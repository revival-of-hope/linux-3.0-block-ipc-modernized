@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify-native.ps1"
exit /b %errorlevel%
