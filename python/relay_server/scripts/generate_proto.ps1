$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RelayDir = Split-Path -Parent $ScriptDir
$RepositoryDir = Split-Path -Parent (Split-Path -Parent $RelayDir)
$ProtocolDir = Join-Path $RepositoryDir "protocol"
$GeneratedDir = Join-Path $RelayDir "generated"
$VenvPython = Join-Path $RelayDir ".venv\Scripts\python.exe"

New-Item -ItemType Directory -Force -Path $GeneratedDir | Out-Null

if (Test-Path $VenvPython) {
    $Python = $VenvPython
} else {
    $Python = "python"
}

& $Python -m grpc_tools.protoc `
    -I $ProtocolDir `
    --python_out=$GeneratedDir `
    (Join-Path $ProtocolDir "vehicle.proto")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "$(Join-Path $GeneratedDir 'vehicle_pb2.py') generated"
