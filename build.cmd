@echo off
rem Build this project inside WSL:
rem   1) native x86_64 build + unit tests
rem   2) aarch64 cross build for the RK3576 board
rem Usage: build.cmd
setlocal
wsl.exe -d Ubuntu --cd "%~dp0." -- bash scripts/build-wsl.sh %*
exit /b %ERRORLEVEL%
