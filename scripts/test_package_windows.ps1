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
    '-noxge', '-AdditionalCookerOptions=-noxgeshadercompile',
    '-map=/Game/SignalCity/Maps/L_SignalCity', '-stage', '-package', '-archive', '-prereqs')) {
    Assert-PackageCondition ($plan.UatArguments -contains $argument) "missing UAT argument $argument"
}
Assert-PackageCondition ($plan.UatArguments -contains "-project=$($plan.Project)") 'project path must remain one argument'
Assert-PackageCondition ($plan.UatArguments -contains "-archivedirectory=$($plan.Destination)") 'archive path mismatch'

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
Assert-PackageCondition ($files['StartServer.ps1'].Contains('Push-Location $PSScriptRoot')) 'server relative output must stay in distribution'
Assert-PackageCondition ($files['READ_ME.txt'].Contains('not acceptance-tested')) 'package preparation must not claim runtime acceptance'

$packagingConfig = Get-Content -LiteralPath (Join-Path $repository 'unreal\DriveIntegration\Config\DefaultGame.ini') -Raw
foreach ($directory in @('/Game/Vehicles', '/Game/Characters/Mannequins/Meshes', '/Game/Characters/Mannequins/Anims')) {
    Assert-PackageCondition ($packagingConfig.Contains("+DirectoriesToAlwaysCook=(Path=`"$directory`")")) "dynamic assets must be cooked: $directory"
}
$moduleRules = Get-Content -LiteralPath (Join-Path $repository 'unreal\DriveIntegration\Source\DriveIntegration\DriveIntegration.Build.cs') -Raw
Assert-PackageCondition ($moduleRules.Contains('RuntimeDependencies.Add("$(ProjectDir)/Config/sensors.json", StagedFileType.NonUFS)')) 'SensorRig JSON must be staged as an exact loose runtime dependency'
$sensorConfig = Join-Path $repository 'unreal\DriveIntegration\Config\sensors.json'
Assert-PackageCondition (Test-Path -LiteralPath $sensorConfig -PathType Leaf) 'SensorRig source config must exist'
$sensorHash = Get-SimCorePackageFileHash -LiteralPath $sensorConfig
Assert-PackageCondition ($sensorHash -match '^[0-9A-F]{64}$') 'file verification must produce SHA-256'
Assert-PackageCondition ($sensorHash -eq (Get-SimCorePackageFileHash -LiteralPath $sensorConfig)) 'same source must hash identically'
Assert-PackageCondition ($sensorHash -ne (Get-SimCorePackageFileHash -LiteralPath $PSCommandPath)) 'different files must not match'
Write-Host 'Windows package plan/template self-test passed. No build, cook or game/server process was started.'
