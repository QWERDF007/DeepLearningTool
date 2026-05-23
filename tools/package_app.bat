@echo off
setlocal

REM Windows 打包入口。这里不依赖 Python，只负责转调用 PowerShell 脚本。
REM 追加的参数会原样传给 package_app.ps1，例如:
REM   tools\package_app.bat -Config release
REM   tools\package_app.bat -SkipWinDeployQt
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_app.ps1" %*
exit /b %errorlevel%
