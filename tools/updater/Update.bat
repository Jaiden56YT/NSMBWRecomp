@echo off
rem NSMBW Online updater: runs Update.ps1 next to it (see that file).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update.ps1"
