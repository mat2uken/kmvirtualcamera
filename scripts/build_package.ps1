<#
.SYNOPSIS
  KM Virtual Camera 配布パッケージビルドスクリプト
.DESCRIPTION
  Release ビルドを実行し、配布用 ZIP パッケージを自動生成します。
#>

[CmdletBinding()]
param(
    [string]$Version = "2.0.0",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$projectRoot = Resolve-Path "$PSScriptRoot\.."
$windowsDir = Join-Path $projectRoot "windows"
$buildDir = Join-Path $windowsDir "build"
$distName = "kmvirtualcamera-windows-v$Version"
$distDir = Join-Path $projectRoot "dist-release" $distName
$zipPath = Join-Path $projectRoot "dist-release" "$distName.zip"

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  KM Virtual Camera Package Builder v$Version" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

# --- Step 1: Build ---
if (-not $SkipBuild) {
    Write-Host "`n[1/5] Building Release..." -ForegroundColor Yellow

    $cmakeBin = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    $env:PATH = "$cmakeBin;$env:PATH"

    if (-not (Test-Path $buildDir)) {
        cmake -B $buildDir -S $windowsDir -G "Visual Studio 17 2022" -A x64
    }
    cmake --build $buildDir --config Release --target VirtualCameraMediaSource Receiver test_vcam_registration
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed!"
        exit 1
    }
    Write-Host "  [OK] Build succeeded" -ForegroundColor Green
} else {
    Write-Host "`n[1/5] Skipping build (--SkipBuild)" -ForegroundColor Gray
}

# --- Step 2: Create dist directory ---
Write-Host "`n[2/5] Assembling package files..." -ForegroundColor Yellow

if (Test-Path $distDir) {
    Remove-Item $distDir -Recurse -Force
}
New-Item -ItemType Directory -Path $distDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $distDir "scripts") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $distDir "vc_redist") -Force | Out-Null

# Copy binaries
$releaseDir = Join-Path $buildDir "Release"
Copy-Item (Join-Path $releaseDir "Receiver.exe") $distDir -Force
Copy-Item (Join-Path $releaseDir "VirtualCameraMediaSource.dll") $distDir -Force
Copy-Item (Join-Path $releaseDir "test_vcam_registration.exe") $distDir -Force

# Copy PowerShell and batch scripts
Copy-Item (Join-Path $projectRoot "scripts" "install.ps1") (Join-Path $distDir "scripts") -Force
Copy-Item (Join-Path $projectRoot "scripts" "uninstall.ps1") (Join-Path $distDir "scripts") -Force
Copy-Item (Join-Path $projectRoot "scripts" "register_vcam.ps1") (Join-Path $distDir "scripts") -Force
Copy-Item (Join-Path $projectRoot "scripts" "unregister_vcam.ps1") (Join-Path $distDir "scripts") -Force
Copy-Item (Join-Path $projectRoot "scripts" "register_vcam.bat") (Join-Path $distDir "scripts") -Force

# Copy VC++ Redistributable
$vcRedist = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\v143\vc_redist.x64.exe"
if (Test-Path $vcRedist) {
    Copy-Item $vcRedist (Join-Path $distDir "vc_redist") -Force
    Write-Host "  [OK] VC++ Redistributable bundled ($('{0:N1}' -f ((Get-Item $vcRedist).Length / 1MB)) MB)" -ForegroundColor Green
} else {
    Write-Warning "vc_redist.x64.exe not found - package will not include VC++ runtime"
}

Write-Host "  [OK] Files assembled" -ForegroundColor Green

# --- Step 3: Generate Pure ASCII install.bat & uninstall.bat ---
Write-Host "`n[3/5] Generating pure ASCII install.bat & uninstall.bat..." -ForegroundColor Yellow

$installBat = @"
@echo off
setlocal
cd /d "%~dp0"
echo ==========================================================
echo  KM Virtual Camera Installer
echo ==========================================================
echo Requesting Administrator privileges to install...
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "Start-Process powershell.exe -ArgumentList '-ExecutionPolicy Bypass -NoProfile -File \"%~dp0scripts\install.ps1\"' -Verb RunAs -Wait"
echo.
endlocal
"@

$uninstallBat = @"
@echo off
setlocal
cd /d "%~dp0"
echo ==========================================================
echo  KM Virtual Camera Uninstaller
echo ==========================================================
echo Requesting Administrator privileges to uninstall...
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "Start-Process powershell.exe -ArgumentList '-ExecutionPolicy Bypass -NoProfile -File \"%~dp0scripts\uninstall.ps1\"' -Verb RunAs -Wait"
echo.
endlocal
"@

# Write as 100% Pure ASCII (No BOM, no multibyte characters) to prevent cmd.exe CP932 syntax errors
$installBatPath = Join-Path $distDir "install.bat"
$uninstallBatPath = Join-Path $distDir "uninstall.bat"
[System.IO.File]::WriteAllText($installBatPath, $installBat, [System.Text.Encoding]::ASCII)
[System.IO.File]::WriteAllText($uninstallBatPath, $uninstallBat, [System.Text.Encoding]::ASCII)

Write-Host "  [OK] Pure ASCII install.bat / uninstall.bat generated" -ForegroundColor Green

# --- Step 4: Generate README.txt ---
Write-Host "`n[4/5] Generating README..." -ForegroundColor Yellow

$readme = @"
==========================================================
 KM Virtual Camera for Windows (v$Version)
 GPU-to-GPU DXGI Zero-Copy Virtual Camera
==========================================================

[動作要件]
- Windows 10 バージョン 1809 以降 / Windows 11
- x64 (64bit) CPU
- GPU: DirectX 11 対応 (Intel / NVIDIA / AMD)

[インストール手順]

1. install.bat をダブルクリック
   → UAC ダイアログで「はい」を選択
   → Visual C++ ランタイムと仮想カメラが自動登録されます

2. Receiver.exe をダブルクリックして起動
   → QR コードが画面に表示されます

3. スマートフォンのブラウザで QR コードを読み取り
   → ブラウザ画面で「送信開始」をタップ
   → 映像が PC の仮想カメラに配信されます

4. Zoom / Teams / Google Meet 等のビデオ会議アプリで
   「WebRTC Bridge Virtual Camera」を選択

[アンインストール]
  uninstall.bat をダブルクリック

[技術仕様]
- GPU-to-GPU DXGI 共有テクスチャ (Direct3D 11) ゼロコピー
- 120fps / 60fps / 30fps フレームレート対応
- H.264 ハードウェアデコード + バッファプール再利用
- MMCSS リアルタイムスレッド優先度
- NV12 / RGB32 デュアルメディアタイプ

[トラブルシューティング]
- 仮想カメラが表示されない場合:
  → install.bat を再実行してください
- 映像が表示されない場合:
  → Receiver.exe が起動していることを確認
  → スマホとPCが同じネットワークに接続されていることを確認

==========================================================
"@
Set-Content -Path (Join-Path $distDir "README.txt") -Value $readme -Encoding UTF8

Write-Host "  [OK] README.txt generated" -ForegroundColor Green

# --- Step 5: Create ZIP ---
Write-Host "`n[5/5] Creating ZIP package..." -ForegroundColor Yellow

if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}
Compress-Archive -Path $distDir -DestinationPath $zipPath -CompressionLevel Optimal

$zipSize = (Get-Item $zipPath).Length
Write-Host "  [OK] Package created: $zipPath" -ForegroundColor Green
Write-Host "  Size: $('{0:N1}' -f ($zipSize / 1MB)) MB" -ForegroundColor Green

# Summary
Write-Host "`n============================================" -ForegroundColor Cyan
Write-Host "  Package Build Complete!" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Output: $zipPath"
Write-Host "  Contents:" -ForegroundColor Gray
Get-ChildItem $distDir -Recurse | ForEach-Object {
    $rel = $_.FullName.Replace($distDir + "\", "")
    if ($_.PSIsContainer) {
        Write-Host "    $rel/" -ForegroundColor Gray
    } else {
        Write-Host "    $rel  ($('{0:N0}' -f ($_.Length / 1KB)) KB)" -ForegroundColor White
    }
}
