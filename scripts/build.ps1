# RollingEPUB host build + test helper.
# Usage: pwsh -File scripts/build.ps1 [-BuildDir build] [-Config Debug] [-Clean]
param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}

cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=$Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (Test-Path (Join-Path $BuildDir "CTestTestfile.cmake")) {
    ctest --test-dir $BuildDir --output-on-failure
}
