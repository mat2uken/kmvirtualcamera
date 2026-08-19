# Clean up all stale/duplicate PnP Virtual Camera devices (SWD\VCAMDEVAPI)
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $scriptDir) { $scriptDir = (Get-Location).Path }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -NoProfile -File `"$scriptDir\cleanup_cameras.ps1`"" -Verb RunAs -Wait
    exit
}

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host " Searching for stale Virtual Camera PnP devices..." -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

$devices = Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like "*VCAMDEVAPI*" }

if ($devices) {
    foreach ($dev in $devices) {
        Write-Host "  [REMOVING] $($dev.InstanceId) ($($dev.FriendlyName))" -ForegroundColor Yellow
        & pnputil /remove-device "$($dev.InstanceId)"
    }
    Write-Host "`n[OK] Successfully removed all stale Virtual Camera devices from Windows!" -ForegroundColor Green
} else {
    Write-Host "[OK] No stale Virtual Camera devices found." -ForegroundColor Green
}

# Restart Windows FrameServer service to refresh OS Settings camera list
Write-Host "`nRestarting Windows FrameServer..." -ForegroundColor Cyan
try {
    Stop-Service -Name "FrameServer" -Force -ErrorAction SilentlyContinue
    Start-Service -Name "FrameServer" -ErrorAction SilentlyContinue
    Write-Host "[OK] FrameServer restarted." -ForegroundColor Green
} catch {
    Write-Host "[WARN] Could not restart FrameServer automatically." -ForegroundColor Yellow
}

Write-Host "`nCleanup Complete! Press Enter to close this window..." -ForegroundColor Green
Read-Host
