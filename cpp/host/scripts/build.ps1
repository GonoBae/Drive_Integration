$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$VcpkgExecutable = Join-Path $RootDir "vcpkg\vcpkg.exe"

if (-not (Test-Path $VcpkgExecutable)) {
    throw "[build] vcpkg not found. Run scripts\setup.ps1 first."
}

Push-Location $RootDir
try {
    cmake --preset release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    cmake --build --preset release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}

Write-Host ""
Write-Host "Build complete under: $RootDir\build"
