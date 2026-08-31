<#
.SYNOPSIS
  Packages the KM Virtual Camera Windows Release binaries and helper scripts into a standalone distribution ZIP.
#>

[CmdletBinding()]
param(
    [string]$Version = "1.0.0",
    [string]$OutputDir = "$PSScriptRoot\..\dist-release"
)

$ErrorActionPreference = "Stop"

$rootDir = (Resolve-Path "$PSScriptRoot\..").Path
$windowsDir = "$rootDir\windows"
$releaseBinDir = "$windowsDir\build\Release"

$outBase = "$rootDir\dist-release"
if (-not (Test-Path $outBase)) {
    New-Item -ItemType Directory -Path $outBase -Force | Out-Null
}

$packageDir = "$outBase\KMVirtualCamera-v$Version-Windows-x64"
$zipPath = "$outBase\KMVirtualCamera-v$Version-Windows-x64.zip"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " KM Virtual Camera - Release Packaging Tool (v$Version)" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Build Windows Release binaries
Write-Host "`n[1/3] Building Windows binaries in Release mode..." -ForegroundColor Yellow
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "$windowsDir\build" --config Release --target Receiver VirtualCameraMediaSource

# Sign Binaries with Developer Authenticode Certificate
$receiverExe = "$releaseBinDir\Receiver.exe"
$vcamDll = "$releaseBinDir\VirtualCameraMediaSource.dll"

Write-Host "Signing Release binaries with Developer Certificate..." -ForegroundColor Yellow
try {
    $cert = Get-ChildItem -Path Cert:\CurrentUser\My -CodeSigningCert | Where-Object { $_.Subject -like "*KM Virtual Camera*" } | Select-Object -First 1
    if (-not $cert) {
        $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=KM Virtual Camera Project, O=KM Virtual Camera, C=JP" -CertStoreLocation "Cert:\CurrentUser\My" -NotAfter (Get-Date).AddYears(5)
    }
    $signtool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"
    if (Test-Path $signtool) {
        & $signtool sign /v /fd SHA256 /a /s My /n "KM Virtual Camera Project" "$receiverExe" | Out-Null
        & $signtool sign /v /fd SHA256 /a /s My /n "KM Virtual Camera Project" "$vcamDll" | Out-Null
        Write-Host "  [OK] Binaries signed with Authenticode signature." -ForegroundColor Green
    }
    # Export public certificate
    $certExportPath = "$packageDir\KMVirtualCamera-Certificate.cer"
} catch {
    Write-Warning "Could not sign binaries: $_"
}

# 2. Re-create package folder
if (Test-Path $packageDir) {
    Remove-Item $packageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

Write-Host "`n[2/3] Staging release files..." -ForegroundColor Yellow

# Copy Binaries
Copy-Item $receiverExe -Destination $packageDir\Receiver.exe -Force
Copy-Item $vcamDll -Destination $packageDir\VirtualCameraMediaSource.dll -Force

# Export public certificate into package
if ($cert) {
    Export-Certificate -Cert $cert -FilePath "$packageDir\KMVirtualCamera-Certificate.cer" -Force | Out-Null
}

# Copy Helper Scripts
Copy-Item "$PSScriptRoot\register_vcam.ps1" -Destination $packageDir\register_vcam.ps1 -Force
Copy-Item "$PSScriptRoot\unregister_vcam.ps1" -Destination $packageDir\unregister_vcam.ps1 -Force
Copy-Item "$PSScriptRoot\run_receiver.ps1" -Destination $packageDir\run_receiver.ps1 -Force

# Create One-Click Batch Launchers for End Users
Set-Content -Path "$packageDir\Start-Receiver.bat" -Value "@echo off`r`ncd /d `"%~dp0`"`r`nstart `"`" `"Receiver.exe`" --url=https://webrtc-bridge-signaling.mat2uken.workers.dev" -Encoding ascii
Set-Content -Path "$packageDir\Register-VirtualCamera.bat" -Value "@echo off`r`ncd /d `"%~dp0`"`r`necho Requesting Administrator privileges to register Virtual Camera...`r`npowershell.exe -ExecutionPolicy Bypass -NoProfile -Command `"Start-Process powershell.exe -ArgumentList '-ExecutionPolicy Bypass -NoProfile -File `\`"%~dp0register_vcam.ps1`\`"' -Verb RunAs`"" -Encoding ascii
Set-Content -Path "$packageDir\Unregister-VirtualCamera.bat" -Value "@echo off`r`ncd /d `"%~dp0`"`r`necho Requesting Administrator privileges to unregister Virtual Camera...`r`npowershell.exe -ExecutionPolicy Bypass -NoProfile -Command `"Start-Process powershell.exe -ArgumentList '-ExecutionPolicy Bypass -NoProfile -File `\`"%~dp0unregister_vcam.ps1`\`"' -Verb RunAs`"" -Encoding ascii
Set-Content -Path "$packageDir\Install-Certificate.bat" -Value "@echo off`r`ncd /d `"%~dp0`"`r`necho Installing Developer Certificate to Trusted Publishers...`r`npowershell.exe -ExecutionPolicy Bypass -NoProfile -Command `"Start-Process powershell.exe -ArgumentList '-ExecutionPolicy Bypass -NoProfile -Command Import-Certificate -FilePath `\`"%~dp0KMVirtualCamera-Certificate.cer`\`" -CertStoreLocation Cert:\LocalMachine\TrustedPublisher; Import-Certificate -FilePath `\`"%~dp0KMVirtualCamera-Certificate.cer`\`" -CertStoreLocation Cert:\LocalMachine\Root' -Verb RunAs`"" -Encoding ascii

# Create README.txt
$readmeContent = @"
==========================================================
 KM Virtual Camera for Windows (v$Version)
==========================================================

【概要】
スマートフォンのカメラ映像・音声を、超低遅延（WebRTC / H.264 / TWCC）で
Windows PC の仮想カメラ（OBS、Teams、Zoom、Google Meet等）および
仮想マイク（VB-CABLE）へリアルタイム転送するアプリケーションです。

----------------------------------------------------------
【クイックスタート手順】
----------------------------------------------------------

1. 仮想カメラのシステム登録（初回のみ）:
   「Register-VirtualCamera.bat」をダブルクリックします。
   （UAC 管理者権限の確認が表示されたら「はい」を押してください）

2. アプリケーションの起動:
   「Start-Receiver.bat」または「Receiver.exe」をダブルクリックして起動します。

3. スマートフォンで接続:
   - 画面上に表示された QR コードをスマートフォンのカメラで読み取ります。
   - ブラウザが開いたら「送信開始」をタップします。
   - すぐにプレビュー画面に低遅延で映像が表示されます。

4. Web会議や配信ソフトでの利用:
   - OBS、Zoom、Teams、Discord、Windows「カメラ」アプリ等で、
     カメラデバイスとして「WebRTC Bridge Virtual Camera」を選択してください。
   - 音声は「VB-Audio Virtual Cable」等を選択することでマイク音声も連携可能です。

----------------------------------------------------------
【登録解除 / アンインストール】
----------------------------------------------------------
「Unregister-VirtualCamera.bat」をダブルクリックして実行してください。

==========================================================
"@

Set-Content -Path "$packageDir\README.txt" -Value $readmeContent -Encoding utf8

# 3. Create ZIP Archive
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Write-Host "Creating ZIP archive: $zipPath..." -ForegroundColor Cyan
Compress-Archive -Path "$packageDir\*" -DestinationPath $zipPath -Force

$zipSize = (Get-Item $zipPath).Length
Write-Host "`n[SUCCESS] Release package created successfully!" -ForegroundColor Green
Write-Host "  ZIP Package: $zipPath ($([math]::Round($zipSize / 1MB, 2)) MB)" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Cyan
