@echo off
title PacManRecomp Easy Build
echo PacManRecomp Easy Build
echo =======================
echo This builds Pac-Man on your PC from your own ROM. It takes a few minutes.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
echo.
pause
