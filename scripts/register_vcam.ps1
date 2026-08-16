<#
.SYNOPSIS
  Registers the WebRTC Bridge Virtual Camera Media Source COM DLL into the Windows Registry.
.DESCRIPTION
  Requires Administrator privileges. Runs regsvr32 on VirtualCameraMediaSource.dll.
#>

[CmdletBinding()]
param(
    [string]$DllPath = "$PSScriptRoot\..\windows\build\Release\VirtualCameraMediaSource.dll"
)

$resolved = Resolve-Path $DllPath -ErrorAction Stop
Write-Host "Registering COM DLL: $resolved" -ForegroundColor Cyan

$process = Start-Process "regsvr32.exe" -ArgumentList "/s `"$resolved`"" -Wait -PassThru
if ($process.ExitCode -eq 0) {
    Write-Host "Virtual Camera COM Media Source registered successfully." -ForegroundColor Green
} else {
    Write-Error "Failed to register DLL. Exit code: $($process.ExitCode). Ensure you are running as Administrator."
}
