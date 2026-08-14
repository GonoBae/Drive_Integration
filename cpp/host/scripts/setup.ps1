$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$VcpkgDir = Join-Path $RootDir "vcpkg"

if (Test-Path $VcpkgDir) {
    Write-Host "[setup] vcpkg already exists, skipping clone."
} else {
    Write-Host "[setup] Cloning vcpkg..."
    git clone https://github.com/microsoft/vcpkg.git $VcpkgDir
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "[setup] Bootstrapping vcpkg..."
& (Join-Path $VcpkgDir "bootstrap-vcpkg.bat") -disableMetrics
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "[setup] Done. vcpkg is ready at: $VcpkgDir"
Write-Host "Next: .\scripts\build.ps1"
