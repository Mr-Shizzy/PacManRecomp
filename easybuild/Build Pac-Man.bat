@echo off
rem PacManRecomp Easy Build (Windows): opens the build window and closes this one.
set PSModulePath=
start "" powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0build.ps1" -Gui
