@echo off
setlocal

cd /d "%~dp0"

echo Building os_sim.exe...
del /q *.o 2>nul
gcc -std=c11 -Wall -Wextra -pedantic -O2 -Iinclude src\*.c -o os_sim.exe
if errorlevel 1 (
    echo.
    echo Build failed. Fix compiler errors, then run this file again.
    pause
    exit /b 1
)

echo.
echo Build succeeded. Starting browser UI server...
echo.
start "" http://localhost:8080/
powershell -ExecutionPolicy Bypass -File "%~dp0web\server.ps1" -Port 8080
