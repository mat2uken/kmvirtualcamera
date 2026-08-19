@echo off
cd /d "%~dp0"
echo Requesting Administrator privileges to register Virtual Camera...
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "Start-Process powershell.exe -ArgumentList '-ExecutionPolicy Bypass -NoProfile -File \"%~dp0register_vcam.ps1\"' -Verb RunAs"
