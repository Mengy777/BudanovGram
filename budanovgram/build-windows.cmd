@echo off
setlocal
call "%~dp0windows-env.cmd"
if errorlevel 1 exit /b 1
python "%~dp0build_windows.py" %*
exit /b %errorlevel%
