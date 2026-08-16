<#
.SYNOPSIS
  Executes the full automated test suite for KM Virtual Camera PoC.
#>

$ErrorActionPreference = "Stop"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " KM Virtual Camera - Full Automated Verification Suite" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Cloudflare Signaling & Browser Frontend Tests (CF-001 - CF-022)
Write-Host "`n[1/3] Running Cloudflare Hono & Durable Object Tests (Vitest)..." -ForegroundColor Yellow
$cloudDir = Resolve-Path "$PSScriptRoot\..\cloud"
Push-Location $cloudDir
npm test
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "Cloud tests failed."
}
npm run build
Pop-Location
Write-Host "[PASS] Cloud tests and web bundle verified." -ForegroundColor Green

# 2. Native Windows Unit Tests (Frame Pipe Protocol & NV12 Converter)
Write-Host "`n[2/3] Running Native Windows Unit Tests (CTest)..." -ForegroundColor Yellow
$windowsDir = Resolve-Path "$PSScriptRoot\..\windows"
Push-Location $windowsDir
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "Native tests failed."
}
Pop-Location
Write-Host "[PASS] Native C++ tests verified." -ForegroundColor Green

# 3. Artifact Verification
Write-Host "`n[3/3] Verifying Built Artifacts..." -ForegroundColor Yellow
$binaries = @(
    "$windowsDir\build\Release\Receiver.exe",
    "$windowsDir\build\Release\VirtualCameraMediaSource.dll",
    "$windowsDir\build\Release\test_frame_pipe_protocol.exe",
    "$windowsDir\build\Release\test_nv12_converter.exe",
    "$cloudDir\dist\public\send\index.html"
)

foreach ($b in $binaries) {
    if (Test-Path $b) {
        $size = (Get-Item $b).Length
        Write-Host "  [OK] Found $b ($([math]::Round($size / 1KB, 2)) KB)" -ForegroundColor Green
    } else {
        Write-Error "Missing expected artifact: $b"
    }
}

Write-Host "`n==========================================================" -ForegroundColor Green
Write-Host " ALL AUTOMATED VERIFICATIONS PASSED SUCCESSFULLY! " -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
