<#
.SYNOPSIS
  Starts the Cloudflare Worker local signaling server using Wrangler.
#>

[CmdletBinding()]
param(
    [int]$Port = 8787
)

$cloudDir = Resolve-Path "$PSScriptRoot\..\cloud"
Write-Host "Building web frontend..." -ForegroundColor Cyan
Set-Location $cloudDir
npm run build:web

Write-Host "Starting Wrangler local dev server on http://127.0.0.1:$Port..." -ForegroundColor Green
npx wrangler dev --port $Port
