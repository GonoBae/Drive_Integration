[CmdletBinding()]
param([switch]$Background)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'server_launcher_common.ps1')

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$runtimeConfigPath = Join-Path $repositoryRoot 'cpp\host\config\signal_city_server.cfg'
$mapPackagePath = Join-Path $repositoryRoot 'map_packages\signal_city_v2'
$requiredPaths = @(
    (Join-Path $mapPackagePath 'manifest.cfg'),
    (Join-Path $mapPackagePath 'ground_heightfield.bin'),
    (Join-Path $mapPackagePath 'ground_surface.csv'),
    (Join-Path $mapPackagePath 'static_colliders.csv'),
    (Join-Path $mapPackagePath 'traffic_network.json')
)
$serverArguments = @(
    '--runtime-config', $runtimeConfigPath,
    '--map-package', $mapPackagePath,
    '--no-demo-entities'
)
$exitCode = Invoke-SimCoreServerLauncher `
    -ProfileName 'Signal city' `
    -RepositoryRoot $repositoryRoot `
    -RuntimeConfigPath $runtimeConfigPath `
    -MapPackagePath $mapPackagePath `
    -RequiredPaths $requiredPaths `
    -ServerArguments $serverArguments `
    -LogPrefix 'simcore-signal-city' `
    -SpawnDescription 'central avenue ENU (0,0), heading north. Two controllers, 4 NPCs, and 8 pedestrians are server-authoritative.' `
    -Background:$Background
if (-not $Background) {
    exit $exitCode
}
