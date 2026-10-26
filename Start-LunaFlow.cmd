@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0build\Release\LunaFlow.exe" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-LunaFlow.ps1"
    if errorlevel 1 (
        echo LunaFlow build failed. See the message above.
        pause
        exit /b 1
    )
)
start "" "%~dp0build\Release\LunaFlow.exe"
