@echo off
cd /d "%~dp0windows\build\Release"
start "" "Receiver.exe" --url=https://webrtc-bridge-signaling.mat2uken.workers.dev
