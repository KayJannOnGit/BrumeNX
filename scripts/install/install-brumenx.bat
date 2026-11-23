@echo off
rem BrumeNX installer for Windows: double-click this file. It runs install-brumenx.ps1 (next to it), which copies the
rem brumenx-*.zip of this folder to your Switch SD card, checks every file and deletes nothing.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-brumenx.ps1" %*
echo.
pause
