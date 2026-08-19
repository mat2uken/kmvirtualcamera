<#
.SYNOPSIS
  Unregisters the WebRTC Bridge Virtual Camera Media Source COM DLL from the Windows Registry.
.DESCRIPTION
  Removes VirtualCameraMediaSource.dll registration from Current User (HKCU) and System (HKLM/HKCR).
#>

[CmdletBinding()]
param(
    [string]$DllPath = ""
)

$ErrorActionPreference = "Continue"

$clsid = "{84BA9D22-C5E5-4674-8848-A979BD2764B2}"
$friendlyName = "WebRTC Bridge Virtual Camera"
Write-Host "Unregistering COM DLL (CLSID: $clsid)..." -ForegroundColor Cyan

$dshowCatGuid = "{860BB310-5D01-11d0-BD3B-00A0C911CE86}"

# 1. Remove HKCU Registry keys
try {
    $hkcuClsidPath = "HKCU:\Software\Classes\CLSID\$clsid"
    if (Test-Path $hkcuClsidPath) {
        Remove-Item -Path $hkcuClsidPath -Recurse -Force
    }
    $hkcuDshowCat = "HKCU:\Software\Classes\CLSID\$dshowCatGuid\Instance\$clsid"
    if (Test-Path $hkcuDshowCat) {
        Remove-Item -Path $hkcuDshowCat -Recurse -Force
    }
    Write-Host "  [OK] Per-User COM & Category registration (HKCU) removed." -ForegroundColor Green
} catch {
    Write-Warning "Failed to remove HKCU registry key: $_"
}

# 2. Remove HKCR / HKLM Registry keys and PnP devices
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    try {
        $scriptPath = (Resolve-Path $PSCommandPath).Path
        Start-Process powershell.exe -ArgumentList "-ExecutionPolicy Bypass -NoProfile -File `"$scriptPath`"" -Verb RunAs -Wait | Out-Null
    } catch {}
} else {
    try {
        $hkcrClsidPath = "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid"
        if (Test-Path $hkcrClsidPath) {
            Remove-Item -Path $hkcrClsidPath -Recurse -Force
        }
        $hkcrDshowCat = "Registry::HKEY_CLASSES_ROOT\CLSID\$dshowCatGuid\Instance\$clsid"
        if (Test-Path $hkcrDshowCat) {
            Remove-Item -Path $hkcrDshowCat -Recurse -Force
        }
        $hklmDshowCat = "HKLM:\SOFTWARE\Classes\CLSID\$dshowCatGuid\Instance\$clsid"
        if (Test-Path $hklmDshowCat) {
            Remove-Item -Path $hklmDshowCat -Recurse -Force
        }
        Remove-Item -Path "HKLM:\SOFTWARE\Classes\MediaFoundation\Transforms\Categories\{E5323777-F976-4F5B-9B55-B94699C46E44}\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -Path "HKLM:\SOFTWARE\Classes\MediaFoundation\Transforms\Categories\{65E8773D-8F56-11D0-A3B9-00A0C9223196}\$clsid" -Recurse -Force -ErrorAction SilentlyContinue

        Write-Host "  [OK] System-wide COM & Category registration (HKCR/HKLM) removed." -ForegroundColor Green
    } catch {
        Write-Warning "Failed to remove HKCR registry key: $_"
    }

    # Clean up any stale PnP Virtual Camera devices (SWD\VCAMDEVAPI)
    try {
        Get-PnpDevice | Where-Object {
            $_.InstanceId -like "*VCAMDEVAPI*" -and $_.FriendlyName -eq $friendlyName
        } | ForEach-Object {
            & pnputil /remove-device $_.InstanceId | Out-Null
        }
        Write-Host "  [OK] Stale Virtual Camera PnP devices cleaned up." -ForegroundColor Green
    } catch {}

    $installedDll = "C:\ProgramData\KMVirtualCamera\VirtualCameraMediaSource.dll"
    if (Test-Path $installedDll) {
        Start-Process "regsvr32.exe" -ArgumentList "/u /s `"$installedDll`"" -Wait | Out-Null
        Remove-Item -Path "C:\ProgramData\KMVirtualCamera" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Virtual Camera unregistration completed." -ForegroundColor Cyan
