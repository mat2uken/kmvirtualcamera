@echo off
cd /d "%~dp0"
echo ========================================================
echo  Cleaning up duplicate/stale Virtual Cameras in Windows...
echo ========================================================
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "Start-Process powershell.exe -ArgumentList '-ExecutionPolicy Bypass -NoProfile -File \"%~dp0cleanup_cameras.ps1\"' -Verb RunAs -Wait"
