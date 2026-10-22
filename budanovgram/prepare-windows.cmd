@echo off
setlocal
call "%~dp0windows-env.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
git submodule update --init --recursive
if errorlevel 1 exit /b 1
call Telegram\build\prepare\win.bat skip-release
exit /b %errorlevel%
