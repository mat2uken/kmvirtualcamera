<#
.SYNOPSIS
  Diagnoses the Windows host environment and readiness for manual testing.
#>

[CmdletBinding()]
param(
    [switch]$LaunchCameraApp
)

$ErrorActionPreference = "Continue"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " KM Virtual Camera - Host Environment Diagnostics" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$windowsDir = Resolve-Path "$PSScriptRoot\..\windows"
$cloudDir = Resolve-Path "$PSScriptRoot\..\cloud"
$clsid = "{84BA9D22-C5E5-4674-8848-A979BD2764B2}"

# 1. Check Built Binaries
Write-Host "`n[1/4] Checking Binary Build Status..." -ForegroundColor Yellow
$receiverExe = "$windowsDir\build\Release\Receiver.exe"
$vcamDll = "$windowsDir\build\Release\VirtualCameraMediaSource.dll"

if (Test-Path $receiverExe) {
    Write-Host "  [OK] Receiver.exe found ($([math]::Round((Get-Item $receiverExe).Length / 1MB, 2)) MB)" -ForegroundColor Green
} else {
    Write-Host "  [MISSING] Receiver.exe not found. Run CMake build." -ForegroundColor Red
}

if (Test-Path $vcamDll) {
    Write-Host "  [OK] VirtualCameraMediaSource.dll found ($([math]::Round((Get-Item $vcamDll).Length / 1KB, 2)) KB)" -ForegroundColor Green
} else {
    Write-Host "  [MISSING] VirtualCameraMediaSource.dll not found. Run CMake build." -ForegroundColor Red
}

# 2. Check COM DLL Registry Registration
Write-Host "`n[2/4] Checking COM DLL Registration (CLSID $clsid)..." -ForegroundColor Yellow
$regPath = "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid\InprocServer32"
if (Test-Path $regPath) {
    $dllRegistered = (Get-ItemProperty -Path $regPath).'(default)'
    Write-Host "  [OK] COM DLL is registered in Registry -> $dllRegistered" -ForegroundColor Green
} else {
    Write-Host "  [NOT REGISTERED] DLL is not yet registered in Registry." -ForegroundColor Yellow
    Write-Host "  --> Run 'pwsh -File scripts/register_vcam.ps1' as Administrator." -ForegroundColor Gray
}

# 3. Check Audio Playback Devices (VB-CABLE)
Write-Host "`n[3/4] Checking Audio Endpoints (VB-CABLE)..." -ForegroundColor Yellow
$audioDevices = Get-CimInstance Win32_SoundDevice -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$hasVbCable = $false
foreach ($dev in $audioDevices) {
    if ($dev -like "*VB-Audio*" -or $dev -like "*CABLE*") {
        $hasVbCable = $true
        Write-Host "  [OK] Found virtual audio device: $dev" -ForegroundColor Green
    }
}
if (-not $hasVbCable) {
    Write-Host "  [INFO] VB-CABLE not detected in sound devices. Audio fallback will play to default speakers or be muted if no cable device." -ForegroundColor Gray
    Write-Host "         (To install VB-CABLE: download from https://vb-audio.com/Cable/)" -ForegroundColor Gray
}

# 4. Check Cloudflare Wrangler
Write-Host "`n[4/4] Checking Cloudflare Wrangler CLI..." -ForegroundColor Yellow
try {
    $wranglerVer = npx --no-install wrangler --version 2>$null
    Write-Host "  [OK] Wrangler CLI available (Version: $wranglerVer)" -ForegroundColor Green
} catch {
    Write-Host "  [INFO] Wrangler CLI not in global path (available in cloud/ directory via npm)." -ForegroundColor Gray
}

# Option: Launch Camera App
if ($LaunchCameraApp) {
    Write-Host "`nLaunching Windows Camera App..." -ForegroundColor Cyan
    Start-Process "microsoft.windows.camera:"
}

Write-Host "`n==========================================================" -ForegroundColor Cyan
Write-Host " DIAGNOSTICS COMPLETED. Read MANUAL_VERIFICATION_GUIDE.md" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
