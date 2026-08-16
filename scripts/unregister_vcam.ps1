<#
.SYNOPSIS
  Unregisters the WebRTC Bridge Virtual Camera Media Source COM DLL from the Windows Registry.
.DESCRIPTION
  Requires Administrator privileges. Runs regsvr32 /u on VirtualCameraMediaSource.dll.
#>

[CmdletBinding()]
param(
    [string]$DllPath = "$PSScriptRoot\..\windows\build\Release\VirtualCameraMediaSource.dll"
)

if (Test-Path $DllPath) {
    $resolved = Resolve-Path $DllPath
    Write-Host "Unregistering COM DLL: $resolved" -ForegroundColor Cyan
    $process = Start-Process "regsvr32.exe" -ArgumentList "/u /s `"$resolved`"" -Wait -PassThru
    if ($process.ExitCode -eq 0) {
        Write-Host "Virtual Camera COM Media Source unregistered successfully." -ForegroundColor Green
    } else {
        Write-Warning "Failed to unregister DLL. Exit code: $($process.ExitCode)."
    }
} else {
    Write-Host "DLL not found at $DllPath; nothing to unregister." -ForegroundColor Yellow
}
