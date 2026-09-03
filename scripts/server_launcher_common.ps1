Set-StrictMode -Version Latest

function Get-SimCoreConfiguredPort {
    param([Parameter(Mandatory)][string]$RuntimeConfigPath)

    $portValues = @(
        foreach ($line in Get-Content -LiteralPath $RuntimeConfigPath) {
            # Match the C++ runtime config grammar: surrounding whitespace and
            # an inline # comment are allowed, while duplicate keys remain an
            # error. Other keys are validated by the server itself.
            if ($line -match '^\s*ws_port\s*=\s*(\d+)\s*(?:#.*)?$') {
                $Matches[1]
            }
        }
    )
    if ($portValues.Count -ne 1) {
        throw "Runtime config must contain exactly one ws_port entry: $RuntimeConfigPath"
    }

    $serverPort = [int]$portValues[0]
    if ($serverPort -lt 1 -or $serverPort -gt 65535) {
        throw "Runtime config ws_port must be in 1..65535: $RuntimeConfigPath"
    }
    return $serverPort
}

function Get-SimCoreListenerProcessIds {
    param([Parameter(Mandatory)][int]$Port)

    # netstat works in the same non-admin shell used by the launch scripts.
    $connections = & "$env:SystemRoot\System32\netstat.exe" -ano -p tcp
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to inspect TCP listeners with netstat.'
    }
    foreach ($connection in $connections) {
        if ($connection -match '^\s*TCP\s+\S+:(\d+)\s+\S+\s+LISTENING\s+(\d+)\s*$') {
            if ([int]$Matches[1] -eq $Port) {
                [int]$Matches[2]
            }
        }
    }
}

function ConvertTo-SimCoreProcessArgument {
    param([Parameter(Mandatory)][string]$Value)

    if ($Value.Contains('"')) {
        throw 'SimCore launcher arguments cannot contain a double quote.'
    }
    return '"' + $Value + '"'
}

function Invoke-SimCoreServerLauncher {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ProfileName,
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$RuntimeConfigPath,
        [Parameter(Mandatory)][string]$MapPackagePath,
        [Parameter(Mandatory)][string[]]$RequiredPaths,
        [hashtable]$MissingFileMessages = @{},
        [Parameter(Mandatory)][string[]]$ServerArguments,
        [Parameter(Mandatory)][string]$LogPrefix,
        [Parameter(Mandatory)][string]$SpawnDescription,
        [switch]$Background
    )

    $serverPath = Join-Path $RepositoryRoot 'cpp\host\build\Release\simcore_publisher.exe'
    foreach ($requiredPath in @($serverPath, $RuntimeConfigPath) + $RequiredPaths) {
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            if ($MissingFileMessages.ContainsKey($requiredPath)) {
                throw [string]$MissingFileMessages[$requiredPath]
            }
            throw "$ProfileName runtime file missing: $requiredPath"
        }
    }

    $serverPort = Get-SimCoreConfiguredPort -RuntimeConfigPath $RuntimeConfigPath
    $listenerProcessIds = @(Get-SimCoreListenerProcessIds -Port $serverPort)
    if ($listenerProcessIds.Count -gt 0) {
        $listenerIds = ($listenerProcessIds | Sort-Object -Unique) -join ', '
        throw "Port $serverPort is already in use (PID: $listenerIds). Stop that server before switching maps; this launcher will not stop it."
    }

    Write-Host "[Launcher] $ProfileName MapPackage: $MapPackagePath"
    Write-Host "[Launcher] Runtime config: $RuntimeConfigPath"
    Write-Host "[Launcher] Spawn: $SpawnDescription"

    if (-not $Background) {
        & $serverPath @ServerArguments
        return $LASTEXITCODE
    }

    $runtimeLogDirectory = Join-Path $RepositoryRoot 'runtime_logs'
    [void](New-Item -ItemType Directory -Path $runtimeLogDirectory -Force)
    $runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' +
        [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $standardOutputPath = Join-Path $runtimeLogDirectory "$LogPrefix-$runId.stdout.log"
    $standardErrorPath = Join-Path $runtimeLogDirectory "$LogPrefix-$runId.stderr.log"
    $quotedArguments = $ServerArguments |
        ForEach-Object { ConvertTo-SimCoreProcessArgument -Value $_ }
    $serverProcess = Start-Process -FilePath $serverPath -ArgumentList $quotedArguments `
        -WorkingDirectory $RepositoryRoot -WindowStyle Hidden `
        -RedirectStandardOutput $standardOutputPath `
        -RedirectStandardError $standardErrorPath -PassThru

    $startupTimer = [Diagnostics.Stopwatch]::StartNew()
    while ($startupTimer.Elapsed.TotalSeconds -lt 10) {
        $serverProcess.Refresh()
        if ($serverProcess.HasExited) {
            throw "$ProfileName server exited during startup (code $($serverProcess.ExitCode)); inspect $standardOutputPath and $standardErrorPath"
        }
        $ownedListener = @(
            Get-SimCoreListenerProcessIds -Port $serverPort |
                Where-Object { $_ -eq $serverProcess.Id }
        )
        if ($ownedListener.Count -gt 0) {
            Write-Host "[Launcher] $ProfileName server ready: PID $($serverProcess.Id), port $serverPort"
            Write-Host "[Launcher] stdout: $standardOutputPath"
            Write-Host "[Launcher] stderr: $standardErrorPath"
            return 0
        }
        Start-Sleep -Milliseconds 200
    }

    # A failed invocation may clean up only the process that it created.
    $serverProcess.Refresh()
    if (-not $serverProcess.HasExited) {
        $serverProcess.Kill()
    }
    throw "$ProfileName server did not bind port $serverPort within 10 seconds; inspect $standardOutputPath and $standardErrorPath"
}
