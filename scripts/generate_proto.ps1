param(
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$RepositoryDir = Split-Path -Parent $PSScriptRoot
$ProtocolDir = Join-Path $RepositoryDir "protocol"
$GeneratedDir = Join-Path $PSScriptRoot "generated"

New-Item -ItemType Directory -Force -Path $GeneratedDir | Out-Null
& $Python -m grpc_tools.protoc `
    -I $ProtocolDir `
    --python_out=$GeneratedDir `
    (Join-Path $ProtocolDir "vehicle.proto")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "$(Join-Path $GeneratedDir 'vehicle_pb2.py') generated"
