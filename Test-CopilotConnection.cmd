@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-CopilotConnection.ps1" %*
pause
