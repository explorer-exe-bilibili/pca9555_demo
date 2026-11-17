@echo off
rem Upload the aarch64 binary to 10.1.1.2 and run a smoke test there.
rem Usage:
rem   deploy.cmd                     (upload + smoke test)
rem   deploy.cmd --bus 7 info        (upload, then run that command on the board)
setlocal
wsl.exe -d Ubuntu --cd "%~dp0." -- bash scripts/deploy-wsl.sh %*
exit /b %ERRORLEVEL%
