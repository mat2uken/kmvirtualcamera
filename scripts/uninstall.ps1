<#
.SYNOPSIS
  KM Virtual Camera Uninstaller Script (Elevated PowerShell)
.DESCRIPTION
  Unregisters the KM Virtual Camera Media Source COM DLL and Windows System Virtual Camera.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Continue"

try {
    [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
    $OutputEncoding = [System.Text.Encoding]::UTF8
} catch {}

# Self-elevation check if run directly without uninstall.bat
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    $scriptPath = if ($PSCommandPath) { (Resolve-Path $PSCommandPath).Path } else { Join-Path $PSScriptRoot "uninstall.ps1" }
    $argList = "-ExecutionPolicy Bypass -NoProfile -File `"$scriptPath`""
    try {
        Start-Process powershell.exe -ArgumentList $argList -Verb RunAs -Wait
        exit
    } catch {
        Write-Warning "管理者権限への昇格が拒否されました。管理者として実行してください。"
    }
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  KM Virtual Camera for Windows Uninstaller" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$unregScriptCandidates = @(
    (Join-Path $PSScriptRoot "unregister_vcam.ps1"),
    (Join-Path $PSScriptRoot "..\scripts\unregister_vcam.ps1"),
    (Join-Path $PWD "scripts\unregister_vcam.ps1"),
    (Join-Path $PWD "unregister_vcam.ps1")
)
$unregScript = $null
foreach ($s in $unregScriptCandidates) {
    if ($s -and (Test-Path $s)) {
        $unregScript = (Resolve-Path $s).Path
        break
    }
}

if ($unregScript) {
    Write-Host "`n仮想カメラを登録解除中 (Unregistering Virtual Camera)..." -ForegroundColor Yellow
    & $unregScript
    Write-Host "`n[OK] アンインストールが完了しました (Uninstallation Complete)." -ForegroundColor Green
} else {
    Write-Error "unregister_vcam.ps1 が見つかりませんでした。"
}

if ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected) {
    Write-Host "`n続行するには何かキーを押してください..." -ForegroundColor Gray
    try {
        [void][System.Console]::ReadKey($true)
    } catch {}
}
