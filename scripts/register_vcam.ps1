<#
.SYNOPSIS
  Registers the WebRTC Bridge Virtual Camera Media Source COM DLL into the Windows Registry.
.DESCRIPTION
  Registers VirtualCameraMediaSource.dll under both Current User (HKCU) and System (HKLM/HKCR).
#>

[CmdletBinding()]
param(
    [string]$DllPath = "$PSScriptRoot\..\windows\build\Release\VirtualCameraMediaSource.dll"
)

$ErrorActionPreference = "Continue"

$resolved = Resolve-Path $DllPath -ErrorAction Stop
Write-Host "Registering COM DLL: $($resolved.Path)" -ForegroundColor Cyan

$clsid = "{84BA9D22-C5E5-4674-8848-A979BD2764B2}"
$friendlyName = "KM Virtual Camera Media Source"

# 1. Per-User COM Registration (HKCU - No Administrator privileges needed!)
try {
    $hkcuClsidPath = "HKCU:\Software\Classes\CLSID\$clsid"
    $hkcuInprocPath = "$hkcuClsidPath\InProcServer32"

    if (-not (Test-Path $hkcuClsidPath)) {
        New-Item -Path $hkcuClsidPath -Force | Out-Null
    }
    Set-ItemProperty -Path $hkcuClsidPath -Name "(Default)" -Value $friendlyName

    if (-not (Test-Path $hkcuInprocPath)) {
        New-Item -Path $hkcuInprocPath -Force | Out-Null
    }
    Set-ItemProperty -Path $hkcuInprocPath -Name "(Default)" -Value $resolved.Path
    Set-ItemProperty -Path $hkcuInprocPath -Name "ThreadingModel" -Value "Both"

    Write-Host "  [OK] Per-User COM registration (HKCU) succeeded." -ForegroundColor Green
} catch {
    Write-Warning "Per-User registry write failed: $_"
}

# 2. System-Wide COM Registration (HKLM/HKCR if elevated)
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($isAdmin) {
    try {
        $hkcrClsidPath = "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid"
        $hkcrInprocPath = "$hkcrClsidPath\InProcServer32"

        if (-not (Test-Path $hkcrClsidPath)) {
            New-Item -Path $hkcrClsidPath -Force | Out-Null
        }
        Set-ItemProperty -Path $hkcrClsidPath -Name "(Default)" -Value $friendlyName

        if (-not (Test-Path $hkcrInprocPath)) {
            New-Item -Path $hkcrInprocPath -Force | Out-Null
        }
        Set-ItemProperty -Path $hkcrInprocPath -Name "(Default)" -Value $resolved.Path
        Set-ItemProperty -Path $hkcrInprocPath -Name "ThreadingModel" -Value "Both"

        Write-Host "  [OK] System-wide COM registration (HKCR) succeeded." -ForegroundColor Green
    } catch {
        Write-Warning "System-wide registry write failed: $_"
    }

    # Execute regsvr32
    try {
        $proc = Start-Process "regsvr32.exe" -ArgumentList "/s `"$($resolved.Path)`"" -Wait -PassThru
        if ($proc.ExitCode -eq 0) {
            Write-Host "  [OK] regsvr32 registration succeeded." -ForegroundColor Green
        }
    } catch {
        Write-Warning "regsvr32 execution failed: $_"
    }
} else {
    Write-Host "  [INFO] Registered in Current User scope (HKCU). For system-wide registration, run as Administrator." -ForegroundColor Gray
}

Write-Host "Virtual Camera registration completed successfully!" -ForegroundColor Green
