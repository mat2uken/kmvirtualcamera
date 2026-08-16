<#
.SYNOPSIS
  Launches the Windows Native Receiver application.
#>

[CmdletBinding()]
param(
    [string]$SignalingUrl = "http://127.0.0.1:8787"
)

$receiverExe = Resolve-Path "$PSScriptRoot\..\windows\build\Release\Receiver.exe" -ErrorAction Stop
Write-Host "Launching KM Virtual Camera Receiver..." -ForegroundColor Cyan
Write-Host "Signaling Server: $SignalingUrl" -ForegroundColor Green

Start-Process -FilePath $receiverExe -ArgumentList "--url=$SignalingUrl"
