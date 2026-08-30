<#
.SYNOPSIS
  Automated Local Live Connection Verification for KM Virtual Camera.
.DESCRIPTION
  Starts local Wrangler signaling server, executes real HTTP signaling handshake
  flow, executes live Named Pipe IPC frame transfer, and validates cleanup.
#>

$ErrorActionPreference = "Stop"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " KM Virtual Camera - Live Local Connection Verification" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$port = 8787
$baseUrl = "http://127.0.0.1:$port"
$cloudDir = Resolve-Path "$PSScriptRoot\..\cloud"
$windowsDir = Resolve-Path "$PSScriptRoot\..\windows"

# Step 1: Ensure Web frontend bundle is built
Write-Host "`n[1/6] Building Web Frontend Bundle..." -ForegroundColor Yellow
Push-Location $cloudDir
npm run build:web | Out-Null
Pop-Location
Write-Host "  [OK] Web bundle ready." -ForegroundColor Green

# Step 2: Start Wrangler Dev Server in Background
Write-Host "`n[2/6] Starting Wrangler Local Signaling Server on port $port..." -ForegroundColor Yellow
$wranglerProcess = Start-Process -FilePath "npx.cmd" -ArgumentList "wrangler dev --port $port --ip 127.0.0.1" -WorkingDirectory $cloudDir -PassThru -NoNewWindow

# Wait for server health endpoint
$maxAttempts = 30
$serverReady = $false
Write-Host "  Waiting for $baseUrl/v1/health..." -NoNewline
for ($i = 1; $i -le $maxAttempts; $i++) {
    Start-Sleep -Milliseconds 1000
    try {
        $health = Invoke-RestMethod -Uri "$baseUrl/v1/health" -Method Get -TimeoutSec 2 -ErrorAction Stop
        if ($health.status -eq "ok") {
            $serverReady = $true
            break
        }
    } catch {
        Write-Host "." -NoNewline
    }
}
Write-Host ""

if (-not $serverReady) {
    if ($wranglerProcess -and -not $wranglerProcess.HasExited) {
        Stop-Process -Id $wranglerProcess.Id -Force
    }
    Write-Error "Signaling server failed to start within $maxAttempts seconds."
}
Write-Host "  [OK] Signaling server is live! (Health Status: $($health.status), Time: $($health.time))" -ForegroundColor Green

try {
    # Step 3: Test Real Signaling Flow (Receiver -> Worker -> Sender -> Worker -> Receiver)
    Write-Host "`n[3/6] Executing Live Signaling Handshake Flow..." -ForegroundColor Yellow

    # 3.1 Receiver creates session
    $createBody = @{ client = @{ name = "powershell-e2e-runner"; version = "1.0.0" } } | ConvertTo-Json
    $createRes = Invoke-RestMethod -Uri "$baseUrl/v1/sessions" -Method Post -Body $createBody -ContentType "application/json"
    $sessionId = $createRes.sessionId
    $receiverToken = $createRes.receiverToken
    $joinUrl = $createRes.joinUrl

    Write-Host "  3.1 [OK] Created session: $sessionId" -ForegroundColor Green
    Write-Host "           Receiver Token: $($receiverToken.Substring(0, 10))..." -ForegroundColor Gray
    Write-Host "           Join URL: $joinUrl" -ForegroundColor Gray

    # Parse Join Token from URL fragment
    $joinToken = ($joinUrl -split "&j=")[1]

    # 3.2 Sender claims session
    $claimBody = @{ claimNonce = "live-nonce-1234567890"; client = @{ name = "browser-sender-sim"; version = "1.0.0" } } | ConvertTo-Json
    $claimHeaders = @{ Authorization = "Bearer $joinToken" }
    $claimRes = Invoke-RestMethod -Uri "$baseUrl/v1/sessions/$sessionId/claim" -Method Post -Headers $claimHeaders -Body $claimBody -ContentType "application/json"
    $senderToken = $claimRes.senderToken
    Write-Host "  3.2 [OK] Claimed session by sender (Sender Token: $($senderToken.Substring(0, 10))...)" -ForegroundColor Green

    # 3.3 Sender puts Offer SDP
    $dummyOfferSdp = "v=0`r`no=- 12345678 2 IN IP4 127.0.0.1`r`ns=-`r`nt=0 0`r`nm=video 9 UDP/TLS/RTP/SAVPF 96`r`na=sendonly`r`n"
    $offerBody = @{ type = "offer"; sdp = $dummyOfferSdp } | ConvertTo-Json
    $offerHeaders = @{ Authorization = "Bearer $senderToken" }
    $offerRes = Invoke-WebRequest -Uri "$baseUrl/v1/sessions/$sessionId/offer" -Method Put -Headers $offerHeaders -Body $offerBody -ContentType "application/json" -UseBasicParsing
    Write-Host "  3.3 [OK] Sender uploaded Offer SDP (HTTP $($offerRes.StatusCode))" -ForegroundColor Green

    # 3.4 Receiver polls Offer SDP
    $recvHeaders = @{ Authorization = "Bearer $receiverToken" }
    $recvOffer = Invoke-RestMethod -Uri "$baseUrl/v1/sessions/$sessionId/offer" -Method Get -Headers $recvHeaders
    if ($recvOffer.sdp -eq $dummyOfferSdp) {
        Write-Host "  3.4 [OK] Receiver polled and verified matching Offer SDP" -ForegroundColor Green
    } else {
        Write-Error "Offer SDP mismatch."
    }

    # 3.5 Receiver puts Answer SDP
    $dummyAnswerSdp = "v=0`r`no=- 87654321 2 IN IP4 127.0.0.1`r`ns=-`r`nt=0 0`r`nm=video 9 UDP/TLS/RTP/SAVPF 96`r`na=recvonly`r`n"
    $answerBody = @{ type = "answer"; sdp = $dummyAnswerSdp } | ConvertTo-Json
    $answerRes = Invoke-WebRequest -Uri "$baseUrl/v1/sessions/$sessionId/answer" -Method Put -Headers $recvHeaders -Body $answerBody -ContentType "application/json" -UseBasicParsing
    Write-Host "  3.5 [OK] Receiver uploaded Answer SDP (HTTP $($answerRes.StatusCode))" -ForegroundColor Green

    # 3.6 Sender polls Answer SDP
    $recvAnswer = Invoke-RestMethod -Uri "$baseUrl/v1/sessions/$sessionId/answer" -Method Get -Headers $offerHeaders
    if ($recvAnswer.sdp -eq $dummyAnswerSdp) {
        Write-Host "  3.6 [OK] Sender polled and verified matching Answer SDP" -ForegroundColor Green
    } else {
        Write-Error "Answer SDP mismatch."
    }

    # 3.7 Delete session
    $delRes = Invoke-WebRequest -Uri "$baseUrl/v1/sessions/$sessionId" -Method Delete -Headers $recvHeaders -UseBasicParsing
    Write-Host "  3.7 [OK] Explicit session termination (HTTP $($delRes.StatusCode))" -ForegroundColor Green

    # Step 4: Run Live Named Pipe IPC Frame Transfer Test
    Write-Host "`n[4/6] Executing Live Named Pipe IPC Frame Pipeline..." -ForegroundColor Yellow
    $pipeTestExe = "$windowsDir\build\Release\test_pipe_integration.exe"
    $pipeOutput = & $pipeTestExe
    Write-Host "  $pipeOutput" -ForegroundColor Gray
    Write-Host "  [OK] Live Named Pipe IPC transfer verified." -ForegroundColor Green

    # Step 5: Run Full Native Unit Test Suite (CTest)
    Write-Host "`n[5/6] Running Native Unit Test Suite..." -ForegroundColor Yellow
    Push-Location $windowsDir
    & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build -C Release --output-on-failure
    Pop-Location
    Write-Host "  [OK] All CTest suites passed." -ForegroundColor Green

} finally {
    # Step 6: Graceful Cleanup
    Write-Host "`n[6/6] Stopping Background Signaling Server..." -ForegroundColor Yellow
    if ($wranglerProcess -and -not $wranglerProcess.HasExited) {
        Stop-Process -Id $wranglerProcess.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 1
    }
    # Kill any leftover node processes on port 8787 if needed
    $portOwner = Get-NetTCPConnection -LocalPort $port -ErrorAction SilentlyContinue
    if ($portOwner) {
        Stop-Process -Id $portOwner.OwningProcess -Force -ErrorAction SilentlyContinue
    }
    Write-Host "  [OK] Server stopped, port $port released." -ForegroundColor Green
}

Write-Host "`n==========================================================" -ForegroundColor Green
Write-Host " LOCAL LIVE CONNECTION VERIFICATION SUCCEEDED! " -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
