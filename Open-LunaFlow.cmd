@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-LunaFlow.ps1" -ConfigureOnly -OpenSolution
if errorlevel 1 (
    echo LunaFlow configuration failed. See the message above.
    pause
    exit /b 1
)
