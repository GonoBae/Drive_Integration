[CmdletBinding()]
param(
    [switch]$DemoEntities,
    [switch]$Background
)

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$serverPath = Join-Path $repositoryRoot "cpp\host\build\Release\simcore_publisher.exe"
$vehicleConfigPath = Join-Path $repositoryRoot "cpp\host\config\vehicle_sedan.cfg"
$mapPackagePath = Join-Path $repositoryRoot "map_packages\landscape_local_v1"
$manifestPath = Join-Path $mapPackagePath "manifest.cfg"
$groundSurfacePath = Join-Path $mapPackagePath "ground_surface.csv"
$staticCollidersPath = Join-Path $mapPackagePath "static_colliders.csv"

if (-not (Test-Path -LiteralPath $serverPath -PathType Leaf)) {
    throw "SimCore server executable not found: $serverPath"
}
if (-not (Test-Path -LiteralPath $vehicleConfigPath -PathType Leaf)) {
    throw "Vehicle config not found: $vehicleConfigPath"
}
if (-not (Test-Path -LiteralPath $groundSurfacePath -PathType Leaf)) {
    throw "Bake ground collision in Unreal first: $groundSurfacePath"
}
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "MapPackage manifest not found; bake ground collision again: $manifestPath"
}
if (-not (Test-Path -LiteralPath $staticCollidersPath -PathType Leaf)) {
    throw "Static collision payload not found: $staticCollidersPath"
}

Write-Host "[Launcher] Landscape MapPackage: $mapPackagePath"
$serverArguments = @(
    "--vehicle-config"
    $vehicleConfigPath
    "--map-package"
    $mapPackagePath
)
if ($DemoEntities) {
    $serverArguments += "--demo-entities"
    Write-Host "[Launcher] Demo runtime entities: enabled (NPC 1001, pedestrian 2001)"
}

if ($Background) {
    $existingServer = Get-Process -Name "simcore_publisher" -ErrorAction SilentlyContinue
    if ($existingServer) {
        $existingIds = ($existingServer.Id | Sort-Object) -join ", "
        throw "SimCore server is already running (PID: $existingIds)"
    }

    $runtimeLogDirectory = Join-Path $repositoryRoot "runtime_logs"
    [void](New-Item -ItemType Directory -Path $runtimeLogDirectory -Force)
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $standardOutputPath = Join-Path $runtimeLogDirectory "simcore-landscape-$timestamp.stdout.log"
    $standardErrorPath = Join-Path $runtimeLogDirectory "simcore-landscape-$timestamp.stderr.log"
    $quotedServerArguments = $serverArguments | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + ($_ -replace '"', '\"') + '"'
        } else {
            $_
        }
    }
    $serverProcess = Start-Process `
        -FilePath $serverPath `
        -ArgumentList $quotedServerArguments `
        -WorkingDirectory $repositoryRoot `
        -WindowStyle Hidden `
        -RedirectStandardOutput $standardOutputPath `
        -RedirectStandardError $standardErrorPath `
        -PassThru
    Start-Sleep -Milliseconds 300
    if ($serverProcess.HasExited) {
        throw "SimCore server exited during startup (code $($serverProcess.ExitCode)); inspect $standardOutputPath and $standardErrorPath"
    }

    Write-Host "[Launcher] SimCore background PID: $($serverProcess.Id)"
    Write-Host "[Launcher] stdout: $standardOutputPath"
    Write-Host "[Launcher] stderr: $standardErrorPath"
    return
}

& $serverPath @serverArguments
exit $LASTEXITCODE
