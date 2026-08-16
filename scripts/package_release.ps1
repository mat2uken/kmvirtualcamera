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

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " KM Virtual Camera - Release Packaging Tool (v$Version)" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$windowsDir = Resolve-Path "$PSScriptRoot\..\windows"
$releaseBinDir = "$windowsDir\build\Release"
$packageDir = "$OutputDir\kmvirtualcamera-windows-v$Version"
$zipPath = "$OutputDir\kmvirtualcamera-windows-v$Version.zip"

# 1. Check binaries
$receiverExe = "$releaseBinDir\Receiver.exe"
$vcamDll = "$releaseBinDir\VirtualCameraMediaSource.dll"

if (-not (Test-Path $receiverExe) -or -not (Test-Path $vcamDll)) {
    Write-Host "Building Windows binaries in Release mode..." -ForegroundColor Yellow
    & "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build $windowsDir\build --config Release
}

# 2. Re-create package folder
if (Test-Path $packageDir) {
    Remove-Item $packageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

Write-Host "`nStaging release files..." -ForegroundColor Yellow

# Copy Binaries
Copy-Item $receiverExe -Destination $packageDir\Receiver.exe -Force
Copy-Item $vcamDll -Destination $packageDir\VirtualCameraMediaSource.dll -Force

# Copy Helper Scripts
Copy-Item "$PSScriptRoot\register_vcam.ps1" -Destination $packageDir\register_vcam.ps1 -Force
Copy-Item "$PSScriptRoot\unregister_vcam.ps1" -Destination $packageDir\unregister_vcam.ps1 -Force
Copy-Item "$PSScriptRoot\run_receiver.ps1" -Destination $packageDir\run_receiver.ps1 -Force

# Create README.txt
$readmeContent = @"
==========================================================
 KM Virtual Camera for Windows (v$Version)
==========================================================

[クイックスタート手順]

1. 仮想カメラの登録 (初回のみ・管理者権限で実行):
   PowerShell を管理者として開き、以下を実行します:
   pwsh -File .\register_vcam.ps1

2. アプリの起動:
   pwsh -File .\run_receiver.ps1
   または Receiver.exe を直接実行します。

3. クラウド / スマホ接続:
   - 画面に表示される QR コードをスマートフォンのブラウザで読み取ります。
   - ブラウザ画面で「送信開始」をタップします。
   - 映像が Windows 上にプレビューされ、仮想カメラおよび仮想マイク (VB-CABLE) へ出力されます。

[アンインストール / 登録解除]:
   pwsh -File .\unregister_vcam.ps1

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
