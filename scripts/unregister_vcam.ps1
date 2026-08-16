<#
.SYNOPSIS
  Unregisters the WebRTC Bridge Virtual Camera Media Source COM DLL from the Windows Registry.
.DESCRIPTION
  Removes VirtualCameraMediaSource.dll registration from Current User (HKCU) and System (HKLM/HKCR).
#>

[CmdletBinding()]
param(
    [string]$DllPath = "$PSScriptRoot\..\windows\build\Release\VirtualCameraMediaSource.dll"
)

$ErrorActionPreference = "Continue"

$clsid = "{84BA9D22-C5E5-4674-8848-A979BD2764B2}"
Write-Host "Unregistering COM DLL (CLSID: $clsid)..." -ForegroundColor Cyan

# 1. Remove HKCU Registry keys
try {
    $hkcuClsidPath = "HKCU:\Software\Classes\CLSID\$clsid"
    if (Test-Path $hkcuClsidPath) {
        Remove-Item -Path $hkcuClsidPath -Recurse -Force
        Write-Host "  [OK] Per-User COM registration (HKCU) removed." -ForegroundColor Green
    }
} catch {
    Write-Warning "Failed to remove HKCU registry key: $_"
}

# 2. Remove HKCR Registry keys if elevated
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($isAdmin) {
    try {
        $hkcrClsidPath = "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid"
        if (Test-Path $hkcrClsidPath) {
            Remove-Item -Path $hkcrClsidPath -Recurse -Force
            Write-Host "  [OK] System-wide COM registration (HKCR) removed." -ForegroundColor Green
        }
    } catch {
        Write-Warning "Failed to remove HKCR registry key: $_"
    }

    if (Test-Path $DllPath) {
        $resolved = Resolve-Path $DllPath
        Start-Process "regsvr32.exe" -ArgumentList "/u /s `"$($resolved.Path)`"" -Wait | Out-Null
    }
}

Write-Host "Virtual Camera unregistration completed." -ForegroundColor Cyan
