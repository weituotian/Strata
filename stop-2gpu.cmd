@echo off
title Strata - stop
echo   Stopping the Strata server and its engine ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0stop-strata.ps1"
echo.
echo   Done. Port 8184 should be free now.
timeout /t 3 /nobreak >nul
