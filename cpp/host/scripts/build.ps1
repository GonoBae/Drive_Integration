$ErrorActionPreference = "Stop"

# Some launchers inject both `Path` and `PATH`. MSBuild treats its inherited
# environment case-insensitively and fails when both keys are present.
$ProcessPath = $env:PATH
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $ProcessPath

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
