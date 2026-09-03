[CmdletBinding()]
param([switch]$Background)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'server_launcher_common.ps1')

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$runtimeConfigPath = Join-Path $repositoryRoot 'cpp\host\config\virtual_city_server.cfg'
$mapPackagePath = Join-Path $repositoryRoot 'map_packages\virtual_city_v1'
$requiredPaths = @(
    (Join-Path $mapPackagePath 'manifest.cfg'),
    (Join-Path $mapPackagePath 'ground_heightfield.bin'),
    (Join-Path $mapPackagePath 'ground_surface.csv'),
    (Join-Path $mapPackagePath 'static_colliders.csv')
)
$serverArguments = @(
    '--runtime-config', $runtimeConfigPath,
    '--map-package', $mapPackagePath,
    '--no-demo-entities'
)
$exitCode = Invoke-SimCoreServerLauncher `
    -ProfileName 'Virtual city' `
    -RepositoryRoot $repositoryRoot `
    -RuntimeConfigPath $runtimeConfigPath `
    -MapPackagePath $mapPackagePath `
    -RequiredPaths $requiredPaths `
    -ServerArguments $serverArguments `
    -LogPrefix 'simcore-virtual-city' `
    -SpawnDescription 'ENU (0,0), heading 90 degrees (east). Lane NPC and signals follow the runtime config; legacy demo entities are disabled.' `
    -Background:$Background
if (-not $Background) {
    exit $exitCode
}
