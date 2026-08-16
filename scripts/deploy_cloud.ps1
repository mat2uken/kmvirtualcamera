<#
.SYNOPSIS
  Builds and deploys the Cloudflare Worker and Browser Sender frontend to Cloudflare.
#>

[CmdletBinding()]
param(
    [string]$Env = "production"
)

$ErrorActionPreference = "Stop"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " KM Virtual Camera - Cloudflare Deployment ($Env)" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$cloudDir = Resolve-Path "$PSScriptRoot\..\cloud"
Push-Location $cloudDir

try {
    # 1. Build Web Frontend with Vite
    Write-Host "`n[1/3] Building Web Frontend (VanJS + Open Props)..." -ForegroundColor Yellow
    npm run build:web

    # 2. Run Test Suite before deployment
    Write-Host "`n[2/3] Running Vitest Suite (CF-001 - CF-022)..." -ForegroundColor Yellow
    npm test

    # 3. Deploy to Cloudflare Workers
    Write-Host "`n[3/3] Deploying Worker and Assets to Cloudflare..." -ForegroundColor Yellow
    npx wrangler deploy

    Write-Host "`n==========================================================" -ForegroundColor Green
    Write-Host " CLOUDFLARE DEPLOYMENT SUCCEEDED! " -ForegroundColor Green
    Write-Host "==========================================================" -ForegroundColor Green
} finally {
    Pop-Location
}
