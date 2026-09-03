[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'server_launcher_common.ps1')

function Assert-SimCoreLauncherCondition {
    param(
        [Parameter(Mandatory)][bool]$Condition,
        [Parameter(Mandatory)][string]$Message
    )

    if (-not $Condition) {
        throw "Server launcher self-test failed: $Message"
    }
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$runtimeConfigs = @(
    (Join-Path $repositoryRoot 'cpp\host\config\runtime_server.cfg'),
    (Join-Path $repositoryRoot 'cpp\host\config\virtual_city_server.cfg'),
    (Join-Path $repositoryRoot 'cpp\host\config\signal_city_server.cfg')
)

foreach ($runtimeConfig in $runtimeConfigs) {
    Assert-SimCoreLauncherCondition `
        -Condition ((Get-SimCoreConfiguredPort -RuntimeConfigPath $runtimeConfig) -eq 9000) `
        -Message "unexpected ws_port in $runtimeConfig"
}

$temporaryConfigPath = Join-Path ([IO.Path]::GetTempPath()) (
    'simcore-launcher-' + [Guid]::NewGuid().ToString('N') + '.cfg'
)
try {
    Set-Content -LiteralPath $temporaryConfigPath -Encoding ASCII -Value @(
        '# The launcher must accept the same whitespace/comment form as C++.'
        '  ws_port = 9107   # local test port'
    )
    Assert-SimCoreLauncherCondition `
        -Condition ((Get-SimCoreConfiguredPort -RuntimeConfigPath $temporaryConfigPath) -eq 9107) `
        -Message 'C++-compatible whitespace or inline comment was rejected'
} finally {
    Remove-Item -LiteralPath $temporaryConfigPath -Force -ErrorAction SilentlyContinue
}

Assert-SimCoreLauncherCondition `
    -Condition ((ConvertTo-SimCoreProcessArgument -Value '--runtime-config') -eq '"--runtime-config"') `
    -Message 'ordinary argument quoting changed'

$rejectedEmbeddedQuote = $false
try {
    [void](ConvertTo-SimCoreProcessArgument -Value 'invalid"argument')
} catch {
    $rejectedEmbeddedQuote = $_.Exception.Message -match 'double quote'
}
Assert-SimCoreLauncherCondition `
    -Condition $rejectedEmbeddedQuote `
    -Message 'an embedded quote was not rejected'

$listenerProcessIds = @(Get-SimCoreListenerProcessIds -Port 9000)
foreach ($listenerProcessId in $listenerProcessIds) {
    Assert-SimCoreLauncherCondition `
        -Condition ($listenerProcessId -is [int]) `
        -Message 'listener discovery returned a non-integer process ID'
}

Write-Host "Server launcher self-test passed ($($runtimeConfigs.Count) profiles)."
