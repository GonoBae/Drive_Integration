[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageDirectory)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$bundle = (Resolve-Path -LiteralPath $PackageDirectory).Path
$serverExe = Join-Path $bundle 'cpp\host\build\Release\simcore_publisher.exe'
$clientExe = Join-Path $bundle 'Windows\DriveIntegration\Binaries\Win64\DriveIntegration.exe'
$serverConfig = Join-Path $bundle 'cpp\host\config\signal_city_server.cfg'
$clientConfig = Join-Path $bundle 'SimCoreClient.ini'
foreach ($file in @($serverExe, $clientExe, $serverConfig, $clientConfig)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Package input missing: $file" }
}
$repository = Split-Path -Parent $PSScriptRoot
$logRoot = Join-Path $repository ('runtime_logs\package-smoke-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
[void](New-Item -ItemType Directory -Path $logRoot)
$clientLog = Join-Path $logRoot 'client.log'
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$testPort = $listener.LocalEndpoint.Port
$listener.Stop()
$serverProcess = $null
$clientProcess = $null
try {
    $serverArguments = '--runtime-config "' + $serverConfig + '" --ws-port ' + $testPort
    $serverProcess = Start-Process -FilePath $serverExe -ArgumentList $serverArguments `
        -WorkingDirectory $logRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logRoot 'server.stdout.log') `
        -RedirectStandardError (Join-Path $logRoot 'server.stderr.log')
    # The real packaged executable is the direct child (not the bootstrap EXE).
    # A different working directory exercises the absolute config/relative-map contract.
    $clientArguments = '-NullRHI -unattended -nosound -nosplash -NoCrashDialog ' +
        '-SimCoreClientConfig="' + $clientConfig + '" -SimCoreServerUrl=ws://127.0.0.1:' +
        $testPort + '/ -abslog="' + $clientLog + '"'
    $clientProcess = Start-Process -FilePath $clientExe -ArgumentList $clientArguments `
        -WorkingDirectory $logRoot -WindowStyle Hidden -PassThru
    Write-Host "[Package smoke] Isolated server PID=$($serverProcess.Id), client PID=$($clientProcess.Id), port=$testPort"
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    $passed = $false
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($serverProcess.HasExited) { throw "Isolated server exited: $($serverProcess.ExitCode)" }
        if ($clientProcess.HasExited) { throw "Packaged client exited: $($clientProcess.ExitCode). Logs: $logRoot" }
        if (Test-Path -LiteralPath $clientLog) {
            $text = Get-Content -LiteralPath $clientLog -Raw
            if ($text -match 'Fatal error:|LogSimCoreClient: Error:|SensorRig disabled:') {
                throw "Packaged client reported a runtime/configuration error. Logs: $logRoot"
            }
            $passed = $text.Contains("endpoint=ws://127.0.0.1:$testPort/") -and
                $text.Contains('Server Hello accepted') -and
                $text.Contains('MapPackage handshake complete') -and
                $text -match 'State telemetry: rate=[1-9][0-9]*\.'
            if ($passed) { break }
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $passed) { throw "Packaged client did not receive validated states within 60 seconds. Logs: $logRoot" }
    Write-Host "PASS packaged client config, map/Hello handshake and live states. Logs: $logRoot"
    Write-Host 'NullRHI startup only: rendering, handling, target-PC performance and 30 minute acceptance NOT tested.'
} finally {
    # Terminate only the direct children created above, never by name or port.
    foreach ($ownedProcess in @($clientProcess, $serverProcess)) {
        if ($null -ne $ownedProcess) {
            $ownedProcess.Refresh()
            if (-not $ownedProcess.HasExited) {
                Stop-Process -InputObject $ownedProcess -ErrorAction SilentlyContinue
                [void]$ownedProcess.WaitForExit(5000)
            }
            $ownedProcess.Dispose()
        }
    }
}
