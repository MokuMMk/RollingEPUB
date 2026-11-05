# Verify that the optional compile-time features really build in every
# reasonable reduced configuration (no tests, just the library target).
# Usage: pwsh -File scripts/check_features.ps1
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
Set-Location $Root

$configs = @(
    @{ Name = "full";        Defs = @() },
    @{ Name = "no-ttf";      Defs = @("-DREPUB_ENABLE_TTF=OFF") },
    @{ Name = "no-images";   Defs = @("-DREPUB_ENABLE_IMAGES=OFF", "-DREPUB_ENABLE_IMAGE_ZOOM=OFF", "-DREPUB_ENABLE_GIF=OFF", "-DREPUB_ENABLE_COVER=OFF") },
    @{ Name = "text-only";   Defs = @("-DREPUB_ENABLE_TTF=OFF", "-DREPUB_ENABLE_IMAGES=OFF", "-DREPUB_ENABLE_IMAGE_ZOOM=OFF", "-DREPUB_ENABLE_GIF=OFF", "-DREPUB_ENABLE_COVER=OFF", "-DREPUB_ENABLE_GRAYSCALE=OFF", "-DREPUB_ENABLE_VERTICAL_TEXT=OFF", "-DREPUB_ENABLE_HYPHENATION=OFF", "-DREPUB_ENABLE_CSS=OFF", "-DREPUB_ENABLE_FOOTNOTES=OFF", "-DREPUB_ENABLE_TABLES=OFF", "-DREPUB_ENABLE_DROP_CAPS=OFF", "-DREPUB_ENABLE_RUBY=OFF", "-DREPUB_ENABLE_BIDI=OFF", "-DREPUB_ENABLE_DITHERING=OFF", "-DREPUB_ENABLE_PROGRESS=OFF", "-DREPUB_ENABLE_TOC=OFF", "-DREPUB_ENABLE_CACHE=OFF", "-DREPUB_BUILD_TESTS=OFF", "-DREPUB_BUILD_TOOLS=OFF") },
    @{ Name = "release";     Defs = @("-DCMAKE_BUILD_TYPE=Release", "-DREPUB_WERROR=ON") }
)

$failed = @()
foreach ($cfg in $configs) {
    $dir = "build-features-$($cfg.Name)"
    Write-Host "=== feature configuration: $($cfg.Name) ===" -ForegroundColor Cyan
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    $args = @("-S", ".", "-B", $dir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug", "-DREPUB_BUILD_TESTS=OFF", "-DREPUB_BUILD_TOOLS=OFF") + $cfg.Defs
    & cmake @args | Out-Null
    if ($LASTEXITCODE -ne 0) { $failed += $cfg.Name; continue }
    & cmake --build $dir --target rolling_epub | Out-Null
    if ($LASTEXITCODE -ne 0) { $failed += $cfg.Name }
    else { Write-Host "    ok" -ForegroundColor Green }
}

if ($failed.Count -gt 0) {
    Write-Host "FAILED configurations: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}
Write-Host "All feature configurations built." -ForegroundColor Green
