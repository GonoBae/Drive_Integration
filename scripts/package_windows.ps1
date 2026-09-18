[CmdletBinding()]
param(
    [string]$EngineRoot,
    [switch]$PrepareOnly,
    [string[]]$ValidationReports = @()
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-SimCorePackageFileHash {
    param([Parameter(Mandatory)][string]$LiteralPath)
    # Avoid optional PowerShell module autoloading in build subprocesses.
    $stream = [IO.File]::OpenRead($LiteralPath)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '')
    } finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
}

function Get-SimCoreWindowsPackagePlan {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$EngineRoot
    )

    $repository = [IO.Path]::GetFullPath($RepositoryRoot)
    $engine = [IO.Path]::GetFullPath($EngineRoot)
    $runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $destination = Join-Path $repository "runtime_tmp\packages\DriveIntegration-$runId"
    $project = Join-Path $repository 'unreal\DriveIntegration\DriveIntegration.uproject'
    [pscustomobject]@{
        Repository = $repository
        Engine = $engine
        Destination = $destination
        Uat = Join-Path $engine 'Engine\Build\BatchFiles\RunUAT.bat'
        Project = $project
        ServerBuild = Join-Path $repository 'cpp\host\build\Release'
        UatArguments = @(
            'BuildCookRun', "-project=$project", '-noP4', '-unattended', '-utf8output',
            '-platform=Win64', '-clientconfig=Development', '-build', '-cook',
            '-noxge', '-AdditionalCookerOptions=-noxgeshadercompile -ShaderWorkingDir=Intermediate/Shaders/PackageWorkingDirectory',
            '-ddc=InstalledNoZenLocalFallback',
            '-map=/Game/SignalCity/Maps/L_SignalCity', '-stage', '-package', '-pak',
            '-iostore', '-compressed', '-prereqs', '-archive', "-archivedirectory=$destination"
        )
    }
}

function Get-SimCorePackageSourceSnapshot {
    param([Parameter(Mandatory)][string]$RepositoryRoot)
    $revision = & git -C $RepositoryRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the source revision.' }
    $changes = @(& git -C $RepositoryRoot status --porcelain --untracked-files=normal)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect working tree changes.' }
    # Include untracked build inputs; a revision alone cannot identify a dirty
    # development candidate. Docs and generated output do not affect this stamp.
    $paths = @(& git -C $RepositoryRoot -c core.quotepath=false ls-files --cached --others --exclude-standard -- `
        cpp/host/CMakeLists.txt cpp/host/CMakePresets.json cpp/host/vcpkg.json cpp/host/src cpp/host/config `
        protocol map_packages/signal_city_v2 unreal/DriveIntegration/Source unreal/DriveIntegration/Config `
        unreal/DriveIntegration/Content unreal/DriveIntegration/DriveIntegration.uproject scripts)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate source files.' }
    $files = @($paths | Sort-Object -Unique | ForEach-Object {
        $path = Join-Path $RepositoryRoot $_
        [ordered]@{ path = $_; sha256 = $(if (Test-Path -LiteralPath $path -PathType Leaf) {
            Get-SimCorePackageFileHash -LiteralPath $path
        } else { 'deleted' }) }
    })
    [ordered]@{ revision = "$revision"; dirty = ($changes.Count -gt 0); files = $files }
}

function Get-SimCorePackageInventory {
    param([Parameter(Mandatory)][string]$PackageRoot)
    $prefix = [IO.Path]::GetFullPath($PackageRoot).TrimEnd('\') + '\'
    @(Get-ChildItem -LiteralPath $PackageRoot -Recurse -File | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($prefix.Length).Replace('\', '/')
        if ($relative -ne 'package_manifest.json') {
            [ordered]@{ path = $relative; bytes = $_.Length
                sha256 = Get-SimCorePackageFileHash -LiteralPath $_.FullName }
        }
    })
}

function Test-SimCorePackageIntegrity {
    param([Parameter(Mandatory)][string]$PackageRoot)
    $prefix = [IO.Path]::GetFullPath($PackageRoot).TrimEnd('\') + '\'
    $manifest = Get-Content -LiteralPath (Join-Path $prefix 'package_manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.format_version -ne 2 -or @($manifest.files).Count -eq 0) {
        throw 'A version 2 package manifest with file hashes is required.'
    }
    $seen = @{}
    foreach ($entry in $manifest.files) {
        $target = [IO.Path]::GetFullPath((Join-Path $prefix $entry.path))
        if (-not $target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or $seen.ContainsKey($target)) {
            throw "Invalid or duplicate package inventory path: $($entry.path)"
        }
        $seen[$target] = $true
        if ($entry.sha256 -notmatch '^[0-9A-Fa-f]{64}$' -or
            -not (Test-Path -LiteralPath $target -PathType Leaf) -or
            (Get-Item -LiteralPath $target).Length -ne $entry.bytes -or
            (Get-SimCorePackageFileHash -LiteralPath $target) -ne $entry.sha256) {
            throw "Package file changed or missing: $($entry.path)"
        }
    }
    return $seen.Count
}

function Get-SimCorePackageLaunchFiles {
    param([Parameter(Mandatory)][ValidateRange(1, 65535)][int]$ServerPort)

    # These scripts deliberately resolve everything from their own distribution
    # directory. No repository path or build-machine drive is embedded.
    @{
        'SimCoreClient.ini' = @"
[SimCoreClient]
ServerUrl=ws://127.0.0.1:$ServerPort/
MapPackageDirectory=map_packages/signal_city_v2
"@
        'StartServer.ps1' = @'
[CmdletBinding()]
param([switch]$Background)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    & (Join-Path $PSScriptRoot 'scripts\run_signal_city_server.ps1') -Background:$Background
    if (-not $Background) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
'@
        'StartClient.ps1' = @'
[CmdletBinding()]
param([switch]$PerformanceCapture, [ValidateRange(1, 3600)][int]$CaptureSeconds = 1800)
$ErrorActionPreference = 'Stop'
$client = Join-Path $PSScriptRoot 'Windows\DriveIntegration.exe'
$config = Join-Path $PSScriptRoot 'SimCoreClient.ini'
foreach ($requiredFile in @($client, $config)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Packaged client file missing: $requiredFile"
    }
}
# A literal quoted INI path also works with Windows PowerShell 5.1 when the
# distribution was moved into a directory containing spaces.
$arguments = '-SimCoreClientConfig="' + $config + '" -windowed -ResX=1920 -ResY=1080'
if ($PerformanceCapture) {
    $arguments += ' -SimCorePerfCapture -SimCorePerfWarmup=10 -SimCorePerfDuration=' + $CaptureSeconds + ' -SimCorePerfLabel=acceptance'
}
$process = Start-Process -FilePath $client -ArgumentList $arguments -WorkingDirectory $PSScriptRoot -PassThru -Wait
exit $process.ExitCode
'@
        'READ_ME.txt' = @'
Drive Integration - Windows Development package

This entire folder can be moved together. Do not move only the game EXE.
Unreal Editor, CMake and Python are not required on the receiving PC.
Windows x64 and a DirectX 12 capable graphics device are required.

If the Microsoft runtime is not installed, run:
  Windows\Engine\Extras\Redist\en-us\vc_redist.x64.exe

Open PowerShell in this folder, then run:
  powershell -NoProfile -ExecutionPolicy Bypass -File .\StartServer.ps1
In another PowerShell window, run:
  powershell -NoProfile -ExecutionPolicy Bypass -File .\StartClient.ps1

Stop the foreground server with Ctrl+C. Existing servers are never replaced.
SimCoreClient.ini contains the local WebSocket endpoint and MapPackage path.
If changing the port, also update cpp\host\config\signal_city_server.cfg.
Only loopback WebSocket addresses are supported for this release.
Keep map_packages and SimCoreClient.ini together: relative map paths are
resolved from the INI, not from the shell working directory.

Optional 30-minute frame/state capture (does not certify manual driving):
  powershell -NoProfile -ExecutionPolicy Bypass -File .\StartClient.ps1 -PerformanceCapture
Verify VSync and frame caps are disabled for performance acceptance.

This package is assembled, not acceptance-tested. Check connection, vehicle
selection, camera, collisions, NPCs, replay, performance and a 30 minute drive
before calling it a release. Check package_manifest.json for build metadata.
'@
    }
}

function Invoke-SimCoreWindowsPackage {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$EngineRoot,
        [switch]$PrepareOnly,
        [string[]]$ValidationReports = @()
    )

    $plan = Get-SimCoreWindowsPackagePlan -RepositoryRoot $RepositoryRoot -EngineRoot $EngineRoot
    $mapRelativeFiles = @('manifest.cfg', 'ground_heightfield.bin', 'ground_surface.csv',
        'static_colliders.csv', 'traffic_network.json', 'drive_route.csv')
    $mapSource = Join-Path $plan.Repository 'map_packages\signal_city_v2'
    $configSource = Join-Path $plan.Repository 'cpp\host\config'
    $sensorConfigSource = Join-Path $plan.Repository 'unreal\DriveIntegration\Config\sensors.json'
    $versionPath = Join-Path $plan.Engine 'Engine\Build\Build.version'
    $requiredFiles = @($plan.Uat, $plan.Project, $versionPath, $sensorConfigSource,
        (Join-Path $configSource 'signal_city_server.cfg'),
        (Join-Path $configSource 'vehicle_sedan.cfg'),
        (Join-Path $plan.Repository 'cpp\host\vcpkg\scripts\buildsystems\vcpkg.cmake'),
        (Join-Path $plan.Repository 'scripts\run_signal_city_server.ps1'),
        (Join-Path $plan.Repository 'scripts\server_launcher_common.ps1'))
    $requiredFiles += @($mapRelativeFiles | ForEach-Object { Join-Path $mapSource $_ })
    foreach ($requiredFile in $requiredFiles) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "Packaging input missing: $requiredFile"
        }
    }
    $version = Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
    if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 6) {
        throw 'This packaging workflow requires Unreal Engine 5.6.'
    }
    [void](Get-Command cmake -ErrorAction Stop)
    . (Join-Path $plan.Repository 'scripts\server_launcher_common.ps1')
    $serverPort = Get-SimCoreConfiguredPort -RuntimeConfigPath (Join-Path $configSource 'signal_city_server.cfg')

    Write-Host "[Package] Destination: $($plan.Destination)"
    Write-Host '[Package] C++ Release server + UE 5.6 Windows Development / L_SignalCity'
    if ($PrepareOnly) {
        Write-Host '[Package] Preparation only: no builds, cook, copies, tests or processes started.'
        return $plan
    }

    foreach ($report in $ValidationReports) {
        if (-not (Test-Path -LiteralPath $report -PathType Leaf)) {
            throw "Validation report missing: $report"
        }
    }
    $sourceSnapshot = Get-SimCorePackageSourceSnapshot -RepositoryRoot $plan.Repository
    $sourceJson = $sourceSnapshot | ConvertTo-Json -Depth 8 -Compress

    # A unique child is created without -Force; an existing destination is never
    # reused, removed or overwritten, even after a failed packaging attempt.
    if (Test-Path -LiteralPath $plan.Destination) {
        throw "Refusing to reuse package destination: $($plan.Destination)"
    }
    [void](New-Item -ItemType Directory -Path $plan.Destination)
    $processSearchPath = $env:PATH
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:Path = $processSearchPath
    Push-Location (Join-Path $plan.Repository 'cpp\host')
    try {
        & cmake --preset release
        if ($LASTEXITCODE -ne 0) { throw "C++ configure failed (exit $LASTEXITCODE)." }
        & cmake --build --preset release --target simcore_publisher
        if ($LASTEXITCODE -ne 0) { throw "C++ Release build failed (exit $LASTEXITCODE)." }
    } finally {
        Pop-Location
    }
    $uatArguments = $plan.UatArguments
    # UAT clears its log directory on entry. Use a fresh candidate-owned child,
    # never the user's shared engine log/backup folder.
    $candidateLogs = Join-Path $plan.Destination 'build_logs'
    [void](New-Item -ItemType Directory -Path $candidateLogs)
    $previousUatLogs = $env:uebp_LogFolder
    $previousFinalUatLogs = $env:uebp_FinalLogFolder
    $previousUatSaved = $env:uebp_EngineSavedFolder
    $previousLocalDdc = [Environment]::GetEnvironmentVariable('UE-LocalDataCachePath', 'Process')
    try {
        $env:uebp_LogFolder = $candidateLogs
        $env:uebp_FinalLogFolder = $candidateLogs
        $env:uebp_EngineSavedFolder = Join-Path $candidateLogs 'uat_saved'
        [void](New-Item -ItemType Directory -Path $env:uebp_EngineSavedFolder)
        [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath',
            (Join-Path $plan.Repository 'runtime_tmp\ue-ddc'), 'Process')
        & $plan.Uat @uatArguments
        if ($LASTEXITCODE -ne 0) { throw "Unreal packaging failed (exit $LASTEXITCODE). Partial output retained: $($plan.Destination)" }
    } finally {
        $env:uebp_LogFolder = $previousUatLogs
        $env:uebp_FinalLogFolder = $previousFinalUatLogs
        $env:uebp_EngineSavedFolder = $previousUatSaved
        [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath', $previousLocalDdc, 'Process')
    }

    # These are the standard UE 5.6 Win64 archive paths. Fail visibly if UAT
    # changes the layout instead of producing a launcher pointing nowhere.
    foreach ($relativeFile in @('Windows\DriveIntegration.exe',
        'Windows\DriveIntegration\Binaries\Win64\DriveIntegration.exe',
        'Windows\DriveIntegration\Config\sensors.json',
        'Windows\Engine\Extras\Redist\en-us\vc_redist.x64.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $plan.Destination $relativeFile) -PathType Leaf)) {
            throw "Required packaged file missing: $relativeFile. Output retained: $($plan.Destination)"
        }
    }
    $sensorConfigDestination = Join-Path $plan.Destination 'Windows\DriveIntegration\Config\sensors.json'
    if ((Get-SimCorePackageFileHash -LiteralPath $sensorConfigSource) -ne
        (Get-SimCorePackageFileHash -LiteralPath $sensorConfigDestination)) {
        throw "Packaged SensorRig config differs from its source: $sensorConfigDestination"
    }
    foreach ($binary in @('simcore_publisher.exe', 'libprotobuf.dll', 'abseil_dll.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $plan.ServerBuild $binary) -PathType Leaf)) {
            throw "Release server binary missing: $binary"
        }
    }
    $serverDestination = Join-Path $plan.Destination 'cpp\host\build\Release'
    $configDestination = Join-Path $plan.Destination 'cpp\host\config'
    $mapDestination = Join-Path $plan.Destination 'map_packages\signal_city_v2'
    $scriptDestination = Join-Path $plan.Destination 'scripts'
    foreach ($directory in @($serverDestination, $configDestination, $mapDestination, $scriptDestination)) {
        [void](New-Item -ItemType Directory -Path $directory)
    }
    Copy-Item -LiteralPath (Join-Path $plan.ServerBuild 'simcore_publisher.exe') -Destination $serverDestination
    # Copy app-local Release DLLs, not build/test EXEs, headers, PDBs or vcpkg.
    # This includes transitive dependencies supplied by vcpkg's app-local step.
    Get-ChildItem -LiteralPath $plan.ServerBuild -Filter '*.dll' -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $serverDestination
    }
    foreach ($config in @('signal_city_server.cfg', 'vehicle_sedan.cfg')) {
        Copy-Item -LiteralPath (Join-Path $configSource $config) -Destination $configDestination
    }
    foreach ($mapFile in $mapRelativeFiles) {
        Copy-Item -LiteralPath (Join-Path $mapSource $mapFile) -Destination $mapDestination
    }
    foreach ($launcher in @('run_signal_city_server.ps1', 'server_launcher_common.ps1')) {
        Copy-Item -LiteralPath (Join-Path $plan.Repository "scripts\$launcher") -Destination $scriptDestination
    }
    $launchFiles = Get-SimCorePackageLaunchFiles -ServerPort $serverPort
    foreach ($entry in $launchFiles.GetEnumerator()) {
        Set-Content -LiteralPath (Join-Path $plan.Destination $entry.Key) -Value $entry.Value -Encoding UTF8
    }
    $finalSource = Get-SimCorePackageSourceSnapshot -RepositoryRoot $plan.Repository
    if ($sourceJson -ne ($finalSource | ConvertTo-Json -Depth 8 -Compress)) {
        throw "Source changed during packaging. Candidate is not coherent; output retained: $($plan.Destination)"
    }
    $snapshotPath = Join-Path $plan.Destination 'source_snapshot.json'
    Set-Content -LiteralPath $snapshotPath -Value $sourceJson -Encoding UTF8
    $evidence = @()
    if ($ValidationReports.Count -gt 0) {
        $evidenceDirectory = Join-Path $plan.Destination 'evidence'
        [void](New-Item -ItemType Directory -Path $evidenceDirectory)
        for ($index = 0; $index -lt $ValidationReports.Count; $index++) {
            $name = '{0:D2}_{1}' -f $index, [IO.Path]::GetFileName($ValidationReports[$index])
            $destination = Join-Path $evidenceDirectory $name
            Copy-Item -LiteralPath $ValidationReports[$index] -Destination $destination
            $evidence += [ordered]@{ path = "evidence/$name"
                sha256 = Get-SimCorePackageFileHash -LiteralPath $destination
                scope = 'prepackage_reference_not_packaged_acceptance' }
        }
    }
    [ordered]@{
        format_version = 2
        created_utc = [DateTime]::UtcNow.ToString('o')
        status = 'assembled_unverified'
        unreal_version = '5.6'
        client_configuration = 'Development'
        server_configuration = 'Release'
        map_id = 'signal_city_v2'
        server_port = $serverPort
        manual_acceptance = 'not_run'
        source_revision = $sourceSnapshot.revision
        source_dirty = $sourceSnapshot.dirty
        source_snapshot = 'source_snapshot.json'
        source_snapshot_sha256 = Get-SimCorePackageFileHash -LiteralPath $snapshotPath
        validation_reports = $evidence
        files = @(Get-SimCorePackageInventory -PackageRoot $plan.Destination)
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $plan.Destination 'package_manifest.json') -Encoding UTF8
    Write-Host "[Package] Assembled: $($plan.Destination)"
    Write-Host '[Package] Builds/cook completed. Runtime, performance and distribution acceptance have NOT been run.'
    return $plan.Destination
}

# Dot-sourcing exposes the pure plan/template functions to the lightweight test.
if ($MyInvocation.InvocationName -ne '.') {
    if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
        throw 'Pass -EngineRoot pointing to the UE_5.6 installation.'
    }
    Invoke-SimCoreWindowsPackage -RepositoryRoot (Split-Path -Parent $PSScriptRoot) `
        -EngineRoot $EngineRoot -PrepareOnly:$PrepareOnly -ValidationReports $ValidationReports
}
