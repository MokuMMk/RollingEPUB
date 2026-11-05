# RollingEPUB end-to-end smoke test.
# Builds, runs unit tests, exercises the CLI on fixtures and validates the
# rendered pages with PIL (non-blank, correct size).
# Usage: pwsh -File scripts/smoke.ps1 [-BuildDir build]
param(
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$cli = Join-Path $BuildDir "repub_cli.exe"
if (-not (Test-Path $cli)) { $cli = Join-Path $BuildDir "repub_cli" }
$outDir = Join-Path $BuildDir "smoke"
New-Item -ItemType Directory -Force $outDir | Out-Null

$books = @("latin.epub", "cjk.epub")
foreach ($book in $books) {
    $path = Join-Path "tests/fixtures/epub" $book
    Write-Host "=== $book ===" -ForegroundColor Cyan
    & $cli info $path
    & $cli toc $path | Select-Object -First 6
    & $cli text $path 0 | Select-Object -First 4
    & $cli notes $path 0
    $png = Join-Path $outDir "$book-page0.png"
    & $cli render $path 0 0 --out $png
    if ($LASTEXITCODE -ne 0) { throw "render failed for $book" }
}

$cover = Join-Path $outDir "cover.png"
& $cli cover (Join-Path "tests/fixtures/epub" "latin.epub") --out $cover

$image = Join-Path $outDir "image.png"
& $cli image (Join-Path "tests/fixtures/epub" "images.epub") 0 "../images/pic.png" --out $image

python scripts/validate_render.py $outDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Smoke test passed." -ForegroundColor Green
