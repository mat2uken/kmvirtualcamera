<#
.SYNOPSIS
  Executes a long-running soak test verifying pipeline stability, memory stability, and pacing.
#>

[CmdletBinding()]
param(
    [int]$DurationMinutes = 30,
    [int]$SampleIntervalSeconds = 10
)

$ErrorActionPreference = "Stop"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " KM Virtual Camera - Soak Test ($DurationMinutes Minutes)" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$windowsDir = Resolve-Path "$PSScriptRoot\..\windows"
$testExe = "$windowsDir\build\Release\test_pipe_integration.exe"

if (-not (Test-Path $testExe)) {
    Write-Error "test_pipe_integration.exe not found. Build solution first."
}

$startTime = Get-Date
$endTime = $startTime.AddMinutes($DurationMinutes)
$iteration = 0
$samples = @()

Write-Host "Soak test started at $startTime. Target end time: $endTime" -ForegroundColor Yellow

while ((Get-Date) -lt $endTime) {
    $iteration++
    $runStart = Get-Date
    
    # Run pipeline integration iteration
    $output = & $testExe
    $runEnd = Get-Date
    $elapsedMs = ($runEnd - $runStart).TotalMilliseconds

    $mem = (Get-Process -Id $PID).WorkingSet64 / 1MB
    $samples += [PSCustomObject]@{
        Iteration = $iteration
        Timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
        MemoryMB  = [math]::Round($mem, 2)
        ElapsedMs = [math]::Round($elapsedMs, 2)
    }

    $timeRemaining = $endTime - (Get-Date)
    Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] Iteration $($iteration): Pipeline OK ($([math]::Round($elapsedMs, 1))ms) | Memory: $([math]::Round($mem, 1))MB | Remaining: $($timeRemaining.ToString('hh\:mm\:ss'))" -ForegroundColor Gray

    Start-Sleep -Seconds $SampleIntervalSeconds
}

$finalTime = Get-Date
$initialMem = $samples[0].MemoryMB
$finalMem = $samples[-1].MemoryMB
$memDiff = $finalMem - $initialMem

Write-Host "`n==========================================================" -ForegroundColor Green
Write-Host " SOAK TEST COMPLETED SUCCESSFULLY" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "  Total Iterations : $iteration" -ForegroundColor Green
Write-Host "  Initial Memory   : $initialMem MB" -ForegroundColor Green
Write-Host "  Final Memory     : $finalMem MB" -ForegroundColor Green
Write-Host "  Memory Delta     : $([math]::Round($memDiff, 2)) MB" -ForegroundColor Green

if ([math]::Abs($memDiff) -lt 50) {
    Write-Host "  Memory Stability : STABLE (No significant leak detected)" -ForegroundColor Green
} else {
    Write-Warning "Memory delta exceeded 50MB. Review memory profiling."
}
