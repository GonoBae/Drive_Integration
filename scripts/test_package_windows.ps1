[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'package_windows.ps1')

function Assert-PackageCondition {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "Windows package self-test failed: $Message" }
}

$repository = Split-Path -Parent $PSScriptRoot
$plan = Get-SimCoreWindowsPackagePlan -RepositoryRoot $repository -EngineRoot 'C:\Engine With Spaces\UE_5.6'
$secondPlan = Get-SimCoreWindowsPackagePlan -RepositoryRoot $repository -EngineRoot 'C:\Engine With Spaces\UE_5.6'
$packageParent = [IO.Path]::GetFullPath((Join-Path $repository 'runtime_tmp\packages')) + '\'
Assert-PackageCondition ($plan.Destination.StartsWith($packageParent, [StringComparison]::OrdinalIgnoreCase)) 'output must stay under ignored packages'
Assert-PackageCondition ($plan.Destination -ne $secondPlan.Destination) 'each run must have an independent destination'
Assert-PackageCondition (-not (Test-Path -LiteralPath $plan.Destination)) 'planning must not create output'
foreach ($argument in @('-platform=Win64', '-clientconfig=Development', '-build', '-cook',
    '-noxge', '-AdditionalCookerOptions=-noxgeshadercompile -ShaderWorkingDir=Intermediate/Shaders/PackageWorkingDirectory',
    '-ddc=InstalledNoZenLocalFallback',
    '-map=/Game/SignalCity/Maps/L_SignalCity', '-stage', '-package', '-archive', '-prereqs')) {
    Assert-PackageCondition ($plan.UatArguments -contains $argument) "missing UAT argument $argument"
}
Assert-PackageCondition ($plan.UatArguments -contains "-project=$($plan.Project)") 'project path must remain one argument'
Assert-PackageCondition ($plan.UatArguments -contains "-archivedirectory=$($plan.Destination)") 'archive path mismatch'
$catalogManifest = Get-Content -LiteralPath (Join-Path $repository 'unreal/DriveIntegration/Config/VehicleCatalog/catalog.json') -Raw | ConvertFrom-Json
$expectedCatalogFiles = 5 + @($catalogManifest.parts).Count + @($catalogManifest.loadouts).Count
Assert-PackageCondition ($plan.VehicleCatalogFiles.Count -eq $expectedCatalogFiles) 'plan must require the manifest, four runtime profiles, and every listed axle part'
foreach ($catalogFile in $plan.VehicleCatalogFiles) {
    Assert-PackageCondition ($catalogFile.SourceRelative.StartsWith('unreal/DriveIntegration/Config/VehicleCatalog/')) 'catalog source must be the shared Unreal config'
    Assert-PackageCondition ($catalogFile.PackageRelative.StartsWith('Windows/DriveIntegration/Config/VehicleCatalog/')) 'catalog must use the exact Unreal NonUFS archive location'
    Assert-PackageCondition (Test-Path -LiteralPath $catalogFile.Source -PathType Leaf) 'required shared catalog source must exist'
}
$sourceRuntimeConfig = Get-Content -LiteralPath (Join-Path $repository 'cpp\host\config\signal_city_server.cfg') -Raw
$packagedRuntimeConfig = ConvertTo-SimCorePackagedRuntimeConfig -Content $sourceRuntimeConfig
$sourceCatalogPath = '../../../unreal/DriveIntegration/Config/VehicleCatalog/catalog.json'
$packagedCatalogPath = '../../../Windows/DriveIntegration/Config/VehicleCatalog/catalog.json'
Assert-PackageCondition ($packagedRuntimeConfig -eq $sourceRuntimeConfig.Replace($sourceCatalogPath, $packagedCatalogPath)) 'packaging must only rewrite the catalog location'
$resolvedPackagedCatalog = [IO.Path]::GetFullPath((Join-Path (Join-Path $plan.Destination 'cpp\host\config') $packagedCatalogPath))
Assert-PackageCondition ($resolvedPackagedCatalog -eq (Join-Path $plan.Destination 'Windows\DriveIntegration\Config\VehicleCatalog\catalog.json')) 'server catalog must resolve to the same files as the packaged client'
foreach ($invalidConfig in @(
    ($sourceRuntimeConfig -replace '(?m)^vehicle_catalog=[^\r\n]*', ''),
    ($sourceRuntimeConfig + "`nvehicle_catalog=$sourceCatalogPath`n"),
    ($sourceRuntimeConfig.Replace($sourceCatalogPath, 'C:/other-catalog/catalog.json')))) {
    $rejected = $false
    try { ConvertTo-SimCorePackagedRuntimeConfig -Content $invalidConfig | Out-Null } catch { $rejected = $true }
    Assert-PackageCondition $rejected 'missing, duplicate or alternate catalog source must fail packaging preflight'
}

$files = Get-SimCorePackageLaunchFiles -ServerPort 9107
Assert-PackageCondition ($files['SimCoreClient.ini'].Contains('ServerUrl=ws://127.0.0.1:9107/')) 'INI port must follow server config'
Assert-PackageCondition ($files['SimCoreClient.ini'].Contains('MapPackageDirectory=map_packages/signal_city_v2')) 'MapPackage must be distribution-relative'
foreach ($name in @('StartServer.ps1', 'StartClient.ps1')) {
    $tokens = $null
    $errors = $null
    [void][Management.Automation.Language.Parser]::ParseInput($files[$name], [ref]$tokens, [ref]$errors)
    Assert-PackageCondition ($errors.Count -eq 0) "generated $name must parse"
    Assert-PackageCondition ($files[$name].Contains('$PSScriptRoot')) "$name must not depend on caller working directory"
    Assert-PackageCondition (-not $files[$name].Contains($repository)) "$name must not embed checkout path"
}
Assert-PackageCondition ($files['StartClient.ps1'].Contains('-SimCoreClientConfig="')) 'INI path must be quoted explicitly'
Assert-PackageCondition ($files['StartClient.ps1'].Contains('[switch]$PerformanceCapture')) 'packaged capture must be opt-in'
Assert-PackageCondition ($files['StartClient.ps1'].Contains('$CaptureSeconds = 1800')) 'acceptance capture must default to thirty minutes'
Assert-PackageCondition ($files['StartServer.ps1'].Contains('Push-Location $PSScriptRoot')) 'server relative output must stay in distribution'
Assert-PackageCondition ($files['READ_ME.txt'].Contains('not acceptance-tested')) 'package preparation must not claim runtime acceptance'

$packagingConfig = Get-Content -LiteralPath (Join-Path $repository 'unreal\DriveIntegration\Config\DefaultGame.ini') -Raw
foreach ($directory in @('/Game/Vehicles', '/Game/Characters/Mannequins/Meshes', '/Game/Characters/Mannequins/Anims')) {
    Assert-PackageCondition ($packagingConfig.Contains("+DirectoriesToAlwaysCook=(Path=`"$directory`")")) "dynamic assets must be cooked: $directory"
}
$moduleRules = Get-Content -LiteralPath (Join-Path $repository 'unreal\DriveIntegration\Source\DriveIntegration\DriveIntegration.Build.cs') -Raw
Assert-PackageCondition ($moduleRules.Contains('RuntimeDependencies.Add("$(ProjectDir)/Config/sensors.json", StagedFileType.NonUFS)')) 'SensorRig JSON must be staged as an exact loose runtime dependency'
Assert-PackageCondition ($moduleRules.Contains('RuntimeDependencies.Add("$(ProjectDir)/Config/VehicleCatalog/" + File, StagedFileType.NonUFS)')) 'shared vehicle catalog must be staged as exact loose runtime files'
foreach ($catalogFile in $plan.VehicleCatalogFiles) {
    $relative = $catalogFile.SourceRelative.Substring('unreal/DriveIntegration/Config/VehicleCatalog/'.Length)
    if ($relative.StartsWith('parts/')) {
        Assert-PackageCondition ($moduleRules.Contains('parts')) 'runtime staging rule must include axle parts'
    } elseif ($relative.StartsWith('loadouts/')) {
        Assert-PackageCondition ($moduleRules.Contains('loadouts')) 'runtime staging rule must include loadouts'
    } else {
        Assert-PackageCondition ($moduleRules.Contains('"' + $relative + '"')) "runtime staging rule missing catalog file $relative"
    }
}
$catalogInventory = @(Get-SimCorePackageInventory -PackageRoot (Join-Path $repository 'unreal\DriveIntegration\Config\VehicleCatalog'))
Assert-PackageCondition ($catalogInventory.Count -eq $expectedCatalogFiles) 'catalog source inventory must hash every shared profile and axle part'
foreach ($entry in $catalogInventory) {
    Assert-PackageCondition ($entry.sha256 -match '^[0-9A-F]{64}' -and $entry.bytes -gt 0) 'catalog source inventory must contain hashes and byte sizes'
}
$sensorConfig = Join-Path $repository 'unreal\DriveIntegration\Config\sensors.json'
Assert-PackageCondition (Test-Path -LiteralPath $sensorConfig -PathType Leaf) 'SensorRig source config must exist'
$sensorHash = Get-SimCorePackageFileHash -LiteralPath $sensorConfig
Assert-PackageCondition ($sensorHash -match '^[0-9A-F]{64}$') 'file verification must produce SHA-256'
Assert-PackageCondition ($sensorHash -eq (Get-SimCorePackageFileHash -LiteralPath $sensorConfig)) 'same source must hash identically'
Assert-PackageCondition ($sensorHash -ne (Get-SimCorePackageFileHash -LiteralPath $PSCommandPath)) 'different files must not match'
$inventory = @(Get-SimCorePackageInventory -PackageRoot (Join-Path $repository 'cpp\host\config'))
Assert-PackageCondition ($inventory.Count -ge 2) 'inventory must contain runtime config files'
foreach ($entry in $inventory) {
    Assert-PackageCondition (-not [IO.Path]::IsPathRooted($entry.path)) 'inventory must be relocatable'
    Assert-PackageCondition ($entry.sha256 -match '^[0-9A-F]{64}$') 'each inventory entry must be hashed'
    Assert-PackageCondition ($entry.bytes -gt 0) 'runtime config inventory must record file size'
    Assert-PackageCondition ($entry.sha256 -eq (Get-SimCorePackageFileHash -LiteralPath `
        (Join-Path (Join-Path $repository 'cpp\host\config') $entry.path))) 'inventory hash mismatch'
}
$packagingScript = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'package_windows.ps1') -Raw
foreach ($field in @('source_revision', 'source_dirty', 'source_snapshot_sha256', 'validation_reports',
    'prepackage_reference_not_packaged_acceptance', '--cached --others --exclude-standard')) {
    Assert-PackageCondition ($packagingScript.Contains($field)) "candidate provenance missing $field"
}
Assert-PackageCondition ($packagingScript.Contains('unreal/DriveIntegration/Config')) 'source snapshot must include tracked and untracked catalog files'
$testParent = [IO.Path]::GetFullPath((Join-Path $repository 'runtime_tmp')) + '\'
$testDirectory = Join-Path $testParent ('package-manifest-test-' + [Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $testDirectory)
try {
    $fixtureFile = Join-Path $testDirectory 'fixture.txt'
    $fixtureManifest = Join-Path $testDirectory 'package_manifest.json'
    Set-Content -LiteralPath $fixtureFile -Value 'synthetic package fixture' -Encoding UTF8
    $fixtureInventory = @(Get-SimCorePackageInventory -PackageRoot $testDirectory)
    $fixture = @{ format_version = 2; files = $fixtureInventory }
    $fixture | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $fixtureManifest -Encoding UTF8
    Assert-PackageCondition ((Test-SimCorePackageIntegrity -PackageRoot $testDirectory) -eq 1) 'valid manifest must pass'
    Set-Content -LiteralPath $fixtureFile -Value 'changed synthetic fixture' -Encoding UTF8
    $rejected = $false
    try { Test-SimCorePackageIntegrity -PackageRoot $testDirectory | Out-Null } catch { $rejected = $true }
    Assert-PackageCondition $rejected 'changed content must fail integrity verification'
    $fixture.files[0].path = '../outside.txt'
    $fixture | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $fixtureManifest -Encoding UTF8
    $rejected = $false
    try { Test-SimCorePackageIntegrity -PackageRoot $testDirectory | Out-Null } catch { $rejected = $true }
    Assert-PackageCondition $rejected 'inventory traversal must be rejected'

    foreach ($catalogFile in $plan.VehicleCatalogFiles) {
        $catalogDestination = Join-Path $testDirectory $catalogFile.PackageRelative
        [void](New-Item -ItemType Directory -Path (Split-Path -Parent $catalogDestination) -Force)
        Copy-Item -LiteralPath $catalogFile.Source -Destination $catalogDestination
    }
    Assert-PackageCondition ((Test-SimCorePackagedVehicleCatalog -RepositoryRoot $repository -PackageRoot $testDirectory) -eq $expectedCatalogFiles) 'exact staged catalog copies must pass source hash verification'
    $stagedCatalogInventory = @(Get-SimCorePackageInventory -PackageRoot $testDirectory | Where-Object { $_.path.StartsWith('Windows/DriveIntegration/Config/VehicleCatalog/') })
    Assert-PackageCondition ($stagedCatalogInventory.Count -eq $expectedCatalogFiles) 'final package inventory must include every listed shared catalog file'
    foreach ($entry in $stagedCatalogInventory) {
        $catalogSource = @($plan.VehicleCatalogFiles | Where-Object { $_.PackageRelative -eq $entry.path })
        Assert-PackageCondition ($catalogSource.Count -eq 1) 'package inventory must map to exactly one catalog source'
        Assert-PackageCondition ($entry.sha256 -eq (Get-SimCorePackageFileHash -LiteralPath $catalogSource[0].Source)) 'staged inventory hash must match shared source bytes'
    }
    $partFile = @($plan.VehicleCatalogFiles | Where-Object { $_.SourceRelative.Contains('/parts/') })[0]
    $stagedPart = Join-Path $testDirectory $partFile.PackageRelative
    [IO.File]::AppendAllText($stagedPart, "`n")
    $rejected = $false
    try { Test-SimCorePackagedVehicleCatalog -RepositoryRoot $repository -PackageRoot $testDirectory | Out-Null } catch { $rejected = $true }
    Assert-PackageCondition $rejected 'changed axle part must fail source hash verification'
    Copy-Item -LiteralPath $partFile.Source -Destination $stagedPart -Force

    $sourceFixture = Join-Path $testDirectory 'source'
    foreach ($catalogFile in $plan.VehicleCatalogFiles) {
        $sourceDestination = Join-Path $sourceFixture $catalogFile.SourceRelative
        [void](New-Item -ItemType Directory -Path (Split-Path -Parent $sourceDestination) -Force)
        Copy-Item -LiteralPath $catalogFile.Source -Destination $sourceDestination
    }
    $sourceManifestPath = Join-Path $sourceFixture 'unreal/DriveIntegration/Config/VehicleCatalog/catalog.json'
    foreach ($invalidParts in @(
        @{ parts = @($catalogManifest.parts[0], $catalogManifest.parts[0]) },
        @{ parts = @('../outside.json') },
        @{ parts = @('parts/missing.json') },
        @{ parts = 'not an array' }
    )) {
        $fixtureCatalog = @{ schema_version = 1; profiles = $catalogManifest.profiles; parts = $invalidParts.parts }
        $fixtureCatalog | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $sourceManifestPath -Encoding UTF8
        $rejected = $false
        try { Get-SimCoreRuntimeVehiclePackageFiles -RepositoryRoot $sourceFixture | Out-Null } catch { $rejected = $true }
        Assert-PackageCondition $rejected 'duplicate, escaping, missing or malformed axle part source must fail preflight'
    }
    $tamperedCatalog = Join-Path $testDirectory $plan.VehicleCatalogFiles[0].PackageRelative
    foreach ($invalidLoadouts in @(
        @{ loadouts = @($catalogManifest.loadouts[0], $catalogManifest.loadouts[0]) },
        @{ loadouts = @('../outside.json') },
        @{ loadouts = @('loadouts/missing.json') },
        @{ loadouts = 'not an array' }
    )) {
        $fixtureCatalog = @{ schema_version = 1; profiles = $catalogManifest.profiles; parts = $catalogManifest.parts; loadouts = $invalidLoadouts.loadouts }
        $fixtureCatalog | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $sourceManifestPath -Encoding UTF8
        $rejected = $false
        try { Get-SimCoreRuntimeVehiclePackageFiles -RepositoryRoot $sourceFixture | Out-Null } catch { $rejected = $true }
        Assert-PackageCondition $rejected 'invalid loadout sources must fail package preflight'
    }
    Set-Content -LiteralPath $tamperedCatalog -Value 'changed catalog fixture' -Encoding UTF8
    $rejected = $false
    try { Test-SimCorePackagedVehicleCatalog -RepositoryRoot $repository -PackageRoot $testDirectory | Out-Null } catch { $rejected = $true }
    Assert-PackageCondition $rejected 'changed staged catalog must fail source hash verification'
    Remove-Item -LiteralPath $tamperedCatalog
    $rejected = $false
    try { Test-SimCorePackagedVehicleCatalog -RepositoryRoot $repository -PackageRoot $testDirectory | Out-Null } catch { $rejected = $true }
    Assert-PackageCondition $rejected 'missing staged catalog must fail source hash verification'
} finally {
    $resolvedTestDirectory = [IO.Path]::GetFullPath($testDirectory)
    if (-not $resolvedTestDirectory.StartsWith($testParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing fixture cleanup outside runtime_tmp.'
    }
    Remove-Item -LiteralPath $resolvedTestDirectory -Recurse -Force
}
Write-Host 'Windows package plan/template self-test passed. No build, cook or game/server process was started.'
