<#
.SYNOPSIS
  Registers the WebRTC Bridge Virtual Camera Media Source COM DLL into the Windows Registry.
.DESCRIPTION
  Deploys and registers the Media Source COM DLL system-wide, then creates the
  camera device through MFCreateVirtualCamera. The COM class is deliberately
  not registered as a DirectShow filter or Media Foundation transform.
#>

[CmdletBinding()]
param(
    [string]$DllPath = ""
)

$ErrorActionPreference = "Continue"

$targetPath = $null
if ($DllPath -and (Test-Path $DllPath)) {
    $targetPath = (Resolve-Path $DllPath).Path
} else {
    $searchLocations = @(
        (Join-Path $PSScriptRoot "..\windows\build\Release\VirtualCameraMediaSource.dll"),
        (Join-Path $PSScriptRoot "..\VirtualCameraMediaSource.dll"),
        (Join-Path $PSScriptRoot "VirtualCameraMediaSource.dll"),
        (Join-Path $PWD "windows\build\Release\VirtualCameraMediaSource.dll"),
        (Join-Path $PWD "VirtualCameraMediaSource.dll"),
        (Join-Path $PSScriptRoot "..\dist-release\bin\VirtualCameraMediaSource.dll")
    )
    foreach ($loc in $searchLocations) {
        if ($loc -and (Test-Path $loc)) {
            $targetPath = (Resolve-Path $loc).Path
            break
        }
    }
}

if (-not $targetPath) {
    Write-Error "VirtualCameraMediaSource.dll not found. Please build the project or specify -DllPath."
    exit 1
}

$resolved = @{ Path = $targetPath }
Write-Host "Registering COM DLL: $($resolved.Path)" -ForegroundColor Cyan

$clsid = "{84BA9D22-C5E5-4674-8848-A979BD2764B2}"
$friendlyName = "WebRTC Bridge Virtual Camera"

# Target deployment directory accessible by NT AUTHORITY\LOCAL SERVICE and UWP AppContainer
$programDataDir = "C:\ProgramData\KMVirtualCamera"
$installedDllPath = Join-Path $programDataDir "VirtualCameraMediaSource.dll"

# Check Administrator privileges
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "  [INFO] Elevating with Administrator privileges to register Virtual Camera..." -ForegroundColor Yellow
    try {
        $scriptPath = if ($PSCommandPath) { (Resolve-Path $PSCommandPath).Path } else { Join-Path $PSScriptRoot "register_vcam.ps1" }
        $argList = "-ExecutionPolicy Bypass -NoProfile -File `"$scriptPath`" -DllPath `"$targetPath`""
        $proc = Start-Process powershell.exe -ArgumentList $argList -Verb RunAs -Wait -PassThru
        if ($proc.ExitCode -eq 0) {
            Write-Host "Virtual Camera registration completed successfully!" -ForegroundColor Green
        } else {
            Write-Warning "Registration finished with exit code: $($proc.ExitCode)"
        }
    } catch {
        Write-Error "Failed to elevate: $_"
    }
    Write-Host "  Please run PowerShell as Administrator and execute: powershell -ExecutionPolicy Bypass -File $scriptPath" -ForegroundColor Yellow
    exit
}

# --- Elevated Execution Block ---
Write-Host "Registering Virtual Camera as Administrator..." -ForegroundColor Cyan

try {
    if (-not (Test-Path $programDataDir)) {
        New-Item -ItemType Directory -Path $programDataDir -Force | Out-Null
    }
    # Stop and kill FrameServer service and processes to release DLL lock completely
    Stop-Service -Name "FrameServer" -Force -ErrorAction SilentlyContinue
    Stop-Service -Name "FrameServerMonitor" -Force -ErrorAction SilentlyContinue
    Get-CimInstance Win32_Service -Filter "Name='FrameServer' or Name='FrameServerMonitor'" | ForEach-Object {
        if ($_.ProcessId -gt 0) {
            Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Milliseconds 500

    Copy-Item -Path $targetPath -Destination $installedDllPath -Force

    # Grant full control permissions to Everyone, LOCAL SERVICE, ALL APPLICATION PACKAGES, and Users
    & icacls.exe $programDataDir /grant '*S-1-1-0:(OI)(CI)F' /T /Q | Out-Null
    & icacls.exe $programDataDir /grant 'NT AUTHORITY\LOCAL SERVICE:(OI)(CI)F' /T /Q | Out-Null
    & icacls.exe $programDataDir /grant 'ALL APPLICATION PACKAGES:(OI)(CI)F' /T /Q | Out-Null
    & icacls.exe $programDataDir /grant 'Users:(OI)(CI)F' /T /Q | Out-Null
    Write-Host "  [OK] Deployed DLL to system directory: $installedDllPath" -ForegroundColor Green
} catch {
    Write-Warning "Failed to deploy DLL to ${programDataDir}: $_"
    $installedDllPath = $targetPath
}

# 1. Remove invalid registrations left by older builds. A per-user CLSID can
# shadow the system CLSID seen by Receiver.exe, while Frame Server loads the
# system registration. DirectShow/MFT category entries create a second, bogus
# camera path that does not implement a DirectShow capture filter.
try {
    $dshowCatGuid = "{860BB310-5D01-11d0-BD3B-00A0C911CE86}"
    $legacyPaths = @(
        "HKCU:\Software\Classes\CLSID\$clsid",
        "HKCU:\Software\Classes\CLSID\$dshowCatGuid\Instance\$clsid",
        "HKLM:\SOFTWARE\Classes\CLSID\$dshowCatGuid\Instance\$clsid",
        "HKLM:\SOFTWARE\Classes\MediaFoundation\Transforms\Categories\{E5323777-F976-4F5B-9B55-B94699C46E44}\$clsid",
        "HKLM:\SOFTWARE\Classes\MediaFoundation\Transforms\Categories\{65E8773D-8F56-11D0-A3B9-00A0C9223196}\$clsid"
    )
    foreach ($legacyPath in $legacyPaths) {
        Remove-Item -LiteralPath $legacyPath -Recurse -Force -ErrorAction SilentlyContinue
    }
    Write-Host "  [OK] Removed stale per-user/category registrations." -ForegroundColor Green
} catch {
    Write-Warning "Legacy registration cleanup failed: $_"
}

# 2. Register only the system-wide COM class used by Frame Server.
try {
    $proc = Start-Process "regsvr32.exe" -ArgumentList "/s `"$installedDllPath`"" -Wait -PassThru
    if ($proc.ExitCode -eq 0) {
        Write-Host "  [OK] System-wide Media Source COM registration succeeded." -ForegroundColor Green
    } else {
        throw "regsvr32 exited with code $($proc.ExitCode)"
    }
} catch {
    Write-Error "regsvr32 execution failed: $_"
    exit 1
}

# 3. Remove an older instance of this camera before registering one device.
try {
    $vcamRegTool = $null
    $vcamRegToolCandidates = @(
        (Join-Path $PSScriptRoot "..\windows\build\Release\test_vcam_registration.exe"),
        (Join-Path $PSScriptRoot "..\test_vcam_registration.exe"),
        (Join-Path $PSScriptRoot "test_vcam_registration.exe")
    )
    foreach ($candidate in $vcamRegToolCandidates) {
        if (Test-Path $candidate) {
            $vcamRegTool = (Resolve-Path $candidate).Path
            break
        }
    }
    if ($vcamRegTool -and (Test-Path $vcamRegTool)) {
        Start-Process $vcamRegTool -ArgumentList "--unregister" -Wait -NoNewWindow | Out-Null
    }
    # Also clean legacy instances created with old category list or session runs.
    Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object {
        $_.InstanceId -like "*VCAMDEVAPI*"
    } | ForEach-Object {
        & pnputil /remove-device $_.InstanceId | Out-Null
    }
    Write-Host "  [OK] Previous WebRTC Bridge camera instances cleaned up." -ForegroundColor Green
} catch {}

# 4. Register 1 clean System Virtual Camera device via test_vcam_registration --register
try {
    if (Test-Path $vcamRegTool) {
        $regProc = Start-Process $vcamRegTool -ArgumentList "--register" -Wait -PassThru -NoNewWindow
        if ($regProc.ExitCode -eq 0) {
            Write-Host "  [OK] Windows System Virtual Camera registered." -ForegroundColor Green
        }
    }
} catch {}

# 5. Restart FrameServer service to discover newly registered virtual camera immediately
try {
    Stop-Service -Name "FrameServer" -Force -ErrorAction SilentlyContinue
    Stop-Service -Name "FrameServerMonitor" -Force -ErrorAction SilentlyContinue
    Get-CimInstance Win32_Service -Filter "Name='FrameServer' or Name='FrameServerMonitor'" | ForEach-Object {
        if ($_.ProcessId -gt 0) {
            Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Milliseconds 500
    Start-Service -Name "FrameServer" -ErrorAction SilentlyContinue
} catch {}

Write-Host "Virtual Camera registration completed successfully!" -ForegroundColor Green
