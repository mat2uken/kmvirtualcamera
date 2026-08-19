@echo off
cd /d "%~dp0\.."
echo ========================================================
echo  KM Virtual Camera - Standalone Capture Verification
echo ========================================================
echo.

if not exist "windows\build\Release\test_vcam_isolated.exe" (
    echo [INFO] Building test_vcam_isolated.exe...
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build windows/build --config Release --target test_vcam_isolated
)

echo [INFO] Running Isolated Virtual Camera Feeder and Capture Test...
echo.
windows\build\Release\test_vcam_isolated.exe

echo.
echo Press any key to close this window...
pause >nul
