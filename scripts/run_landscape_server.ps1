[CmdletBinding()]
param(
    [string]$RuntimeConfigPath,
    [switch]$DemoEntities,
    [switch]$NoDemoEntities,
    [switch]$Background
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'server_launcher_common.ps1')

if ($DemoEntities -and $NoDemoEntities) {
    throw '-DemoEntities and -NoDemoEntities cannot be used together'
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$serverPath = Join-Path $repositoryRoot 'cpp\host\build\Release\simcore_publisher.exe'
$vehicleConfigPath = Join-Path $repositoryRoot 'cpp\host\config\vehicle_sedan.cfg'
if ([string]::IsNullOrWhiteSpace($RuntimeConfigPath)) {
    $RuntimeConfigPath = Join-Path $repositoryRoot 'cpp\host\config\runtime_server.cfg'
}
$RuntimeConfigPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
    $RuntimeConfigPath
)
$mapPackagePath = Join-Path $repositoryRoot 'map_packages\landscape_local_v1'
$manifestPath = Join-Path $mapPackagePath 'manifest.cfg'
$groundSurfacePath = Join-Path $mapPackagePath 'ground_surface.csv'
$staticCollidersPath = Join-Path $mapPackagePath 'static_colliders.csv'
$requiredPaths = @(
    $vehicleConfigPath,
    $groundSurfacePath,
    $manifestPath,
    $staticCollidersPath
)
$missingFileMessages = @{
    $serverPath = "SimCore server executable not found: $serverPath"
    $vehicleConfigPath = "Vehicle config not found: $vehicleConfigPath"
    $groundSurfacePath = "Bake ground collision in Unreal first: $groundSurfacePath"
    $manifestPath = "MapPackage manifest not found; bake ground collision again: $manifestPath"
    $staticCollidersPath = "Static collision payload not found: $staticCollidersPath"
}
$serverArguments = @(
    '--runtime-config', $RuntimeConfigPath,
    '--vehicle-config', $vehicleConfigPath,
    '--map-package', $mapPackagePath
)
$demoDescription = 'runtime config default'
if ($DemoEntities) {
    $serverArguments += '--demo-entities'
    $demoDescription = 'enabled by CLI override'
} elseif ($NoDemoEntities) {
    $serverArguments += '--no-demo-entities'
    $demoDescription = 'disabled by CLI override'
}
$exitCode = Invoke-SimCoreServerLauncher `
    -ProfileName 'Landscape' `
    -RepositoryRoot $repositoryRoot `
    -RuntimeConfigPath $RuntimeConfigPath `
    -MapPackagePath $mapPackagePath `
    -RequiredPaths $requiredPaths `
    -MissingFileMessages $missingFileMessages `
    -ServerArguments $serverArguments `
    -LogPrefix 'simcore-landscape' `
    -SpawnDescription "configured Landscape origin; demo entities $demoDescription." `
    -Background:$Background
if (-not $Background) {
    exit $exitCode
}
