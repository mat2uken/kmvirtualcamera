# Automated End-to-End System Verification Suite for KM Virtual Camera
# Tests: Cloudflare Static Assets, Signaling Health, Official QR Decode Roundtrip, Pipe Protocol, NV12, Receiver Startup

param(
    [string]$SignalingUrl = "https://webrtc-bridge-signaling.mat2uken.workers.dev"
)

$ErrorActionPreference = "Stop"
$rootDir = (Get-Item $PSScriptRoot).Parent.FullName

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  KM VIRTUAL CAMERA - AUTOMATED E2E VERIFICATION SUITE      " -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "Project Root: $rootDir"
Write-Host "Signaling Server: $SignalingUrl`n"

$allPassed = $true
Push-Location $rootDir

function Run-Step([string]$title, [scriptblock]$action) {
    Write-Host "[TEST STEP] $title ..." -ForegroundColor Yellow
    try {
        & $action
        Write-Host "  -> [PASS] $title`n" -ForegroundColor Green
    } catch {
        Write-Host "  -> [FAIL] $title : $_`n" -ForegroundColor Red
        $script:allPassed = $false
    }
}

# 1. Cloudflare Worker Static Web Assets & Health
Run-Step "1. Cloudflare Worker API & Static Assets (/send/)" {
    $health = Invoke-RestMethod -Uri "$SignalingUrl/v1/health" -Method Get -TimeoutSec 10
    if ($health.status -ne "ok") {
        throw "Signaling server health check failed: status is not 'ok'"
    }

    $sendPage = Invoke-WebRequest -Uri "$SignalingUrl/send/" -Method Get -TimeoutSec 10
    if ($sendPage.StatusCode -ne 200 -or -not ($sendPage.Content -like "*KM Virtual Camera*")) {
        throw "Static web sender (/send/) failed to return HTML with KM Virtual Camera title"
    }
    Write-Host "     Signaling Health: OK ($($health.version))" -ForegroundColor Gray
    Write-Host "     Static Web Sender (/send/): HTTP 200 OK" -ForegroundColor Gray
}

# 2. Official Nayuki QR Generator & Automated jsQR Decode Roundtrip
Run-Step "2. Official QR Code Generator & jsQR Automated Decode Roundtrip" {
    $qrExe = Join-Path $rootDir "windows\build\Release\test_qr_generator.exe"
    $bmpPath = Join-Path $rootDir "test_generated_qr.bmp"
    if (Test-Path $bmpPath) { Remove-Item $bmpPath -Force }
    
    if (-not (Test-Path $qrExe)) {
        throw "test_qr_generator.exe not found at $qrExe"
    }
    
    $qrOut = & $qrExe
    if (-not (Test-Path $bmpPath)) {
        $bmpPathAlt = Join-Path $rootDir "windows\tests\test_generated_qr.bmp"
        if (Test-Path $bmpPathAlt) {
            $bmpPath = $bmpPathAlt
        } else {
            throw "Failed to generate test_generated_qr.bmp"
        }
    }

    $decodeScript = Join-Path $rootDir "scripts\verify_qr_decoder.mjs"
    $nodeOut = & node $decodeScript $bmpPath 2>&1
    Write-Host "     $($nodeOut | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "jsQR automated decoding failed on generated QR bitmap"
    }
}

# 3. Media Foundation H.264 Video Decoder Test
Run-Step "3. Media Foundation H.264 Video Decoder (MFT) Unit Test" {
    $exe = Join-Path $rootDir "windows\build\Release\test_h264_decoder.exe"
    $out = & $exe 2>&1
    Write-Host "     $($out | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "test_h264_decoder failed"
    }

    $depackExe = Join-Path $rootDir "windows\build\Release\test_h264_depacketizer.exe"
    $depackOut = & $depackExe 2>&1
    Write-Host "     $($depackOut | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "test_h264_depacketizer failed"
    }
}

# 4. Frame Pipe Protocol Test
Run-Step "4. Named Pipe Frame Protocol Unit Test" {
    $exe = Join-Path $rootDir "windows\build\Release\test_frame_pipe_protocol.exe"
    $out = & $exe 2>&1
    Write-Host "     $($out | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "test_frame_pipe_protocol failed"
    }
}

# 5. NV12 Color Converter Test
Run-Step "5. NV12 Color Converter Unit Test" {
    $exe = Join-Path $rootDir "windows\build\Release\test_nv12_converter.exe"
    $out = & $exe 2>&1
    Write-Host "     $($out | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "test_nv12_converter failed"
    }
}

# 6. Named Pipe End-to-End Integration Test
Run-Step "6. Named Pipe Client-Server End-to-End Pipeline Test" {
    $exe = Join-Path $rootDir "windows\build\Release\test_pipe_integration.exe"
    $out = & $exe 2>&1
    Write-Host "     $($out | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "test_pipe_integration failed"
    }
}

# 7. Receiver Startup & Lifecycle Test
Run-Step "7. Receiver Startup, Signaling & Virtual Camera Lifecycle Test" {
    $exe = Join-Path $rootDir "windows\build\Release\test_receiver_startup.exe"
    $out = & $exe 2>&1
    Write-Host "     $($out | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "test_receiver_startup failed"
    }
}

# 8. WebRTC DTLS Handshake & Offer/Answer Loopback Test
Run-Step "8. WebRTC DTLS Handshake & Role Negotiation Unit Test" {
    $exe = Join-Path $rootDir "windows\build\Release\test_webrtc_dtls.exe"
    if (-not (Test-Path $exe)) {
        throw "test_webrtc_dtls.exe not found at $exe"
    }
    $out = & $exe 2>&1
    Write-Host "     $($out | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "test_webrtc_dtls failed"
    }
}

# 9. Bandwidth Estimator & ABR Adaptive Bitrate Control Test
Run-Step "9. Bandwidth Estimator & ABR Adaptive Bitrate Control Test" {
    $exe = Join-Path $rootDir "windows\build\Release\test_bandwidth_estimator.exe"
    if (-not (Test-Path $exe)) {
        throw "test_bandwidth_estimator.exe not found at $exe"
    }
    $out = & $exe 2>&1
    Write-Host "     $($out | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "test_bandwidth_estimator failed"
    }
}

# 10. Playwright Automated Browser Streaming E2E Test
Run-Step "10. Playwright Automated Browser Streaming & Answer SDP Validation" {
    $script = Join-Path $rootDir "scripts\test_browser_stream_e2e.mjs"
    $out = & node $script 2>&1
    Write-Host "     $($out | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "Playwright automated browser streaming test failed"
    }
}

# 11. Live Receiver.exe + Playwright Browser E2E Integration Test (Real Hardware Video Pipeline)
Run-Step "11. Live Receiver.exe + Playwright Browser E2E Integration Test" {
    $script = Join-Path $rootDir "scripts\test_live_receiver_and_browser.mjs"
    $out = & node $script 2>&1
    Write-Host "     $($out | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "Live Receiver + Browser E2E Integration test failed"
    }
}

# 12. Virtual Camera Media Source & Windows Source Reader Consumer Pipeline Test
Run-Step "12. Virtual Camera Media Source & Windows Source Reader Integration Test" {
    $exe = Join-Path $rootDir "windows\build\Release\test_virtual_camera_e2e.exe"
    if (-not (Test-Path $exe)) {
        throw "test_virtual_camera_e2e.exe not found at $exe"
    }
    $out = & $exe 2>&1
    Write-Host "     $($out | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "Virtual Camera E2E test failed"
    }
}

# 13. Virtual Camera Pixel-Level 100% Fidelity & Dynamic Switching Automated Test
Run-Step "13. Virtual Camera Pixel-Level 100% Exact Match Fidelity Test" {
    $exe = Join-Path $rootDir "windows\build\Release\test_vcam_pixel_fidelity.exe"
    if (-not (Test-Path $exe)) {
        throw "test_vcam_pixel_fidelity.exe not found at $exe"
    }
    $out = & $exe 2>&1
    Write-Host "     $($out | Out-String)" -ForegroundColor Gray
    if ($LASTEXITCODE -ne 0) {
        throw "Virtual Camera Pixel Fidelity test failed"
    }
}

Write-Host "============================================================" -ForegroundColor Cyan
if ($allPassed) {
    Write-Host "  >>> ALL AUTOMATED VERIFICATION TESTS PASSED (100%) <<<    " -ForegroundColor Green
    Write-Host "============================================================" -ForegroundColor Cyan
    exit 0
} else {
    Write-Host "  >>> SOME AUTOMATED VERIFICATION TESTS FAILED <<<          " -ForegroundColor Red
    Write-Host "============================================================" -ForegroundColor Cyan
    exit 1
}

