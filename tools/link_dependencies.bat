@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

REM DeepLearningTool Windows dependency link script.
REM This script does not require Python. It uses mklink directly.

pushd "%~dp0.." || exit /b 1

call :link_dltool
if errorlevel 1 exit /b 1
echo link dltool dll success

call :link_sqlite
if errorlevel 1 exit /b 1
echo link sqlite3 dll success

call :link_test
if errorlevel 1 exit /b 1
echo link test success

popd
exit /b 0

:link_dltool
call :link_dir "build\bin\dltool" "build\dltool"
if errorlevel 1 exit /b 1

if exist "build\bin\dltool\" (
    for /r "build\bin\dltool" %%F in (*.dll) do (
        call :link_file "%%~fF" "build\bin\%%~nxF"
        if errorlevel 1 exit /b 1
    )
) else (
    echo skip dltool module dll links, missing build\bin\dltool
)
exit /b 0

:link_sqlite
set "SQLITE_ROOT="
if exist "cmake\ConfigSQLite.cmake" (
    for /f tokens^=2^ delims^=^" %%A in ('findstr /c:"set(CMAKE_PREFIX_PATH" "cmake\ConfigSQLite.cmake"') do (
        set "SQLITE_ROOT=%%A"
    )
)

if not defined SQLITE_ROOT (
    echo skip sqlite3 link, CMAKE_PREFIX_PATH was not found in cmake\ConfigSQLite.cmake
    exit /b 0
)

set "SQLITE_DLL=!SQLITE_ROOT:/=\!\lib\sqlite3.dll"
call :link_file "!SQLITE_DLL!" "build\bin\sqlite3.dll"
exit /b %errorlevel%

:link_test
if exist "build\tests\" (
    call :link_dir "build\tests\dltool" "build\dltool"
    exit /b %errorlevel%
)

echo skip test link, missing build\tests
exit /b 0

:link_file
set "TARGET=%~1"
set "LINK=%~2"
for %%I in ("%TARGET%") do set "TARGET_ABS=%%~fI"
for %%I in ("%LINK%") do set "LINK_ABS=%%~fI"

if not exist "!TARGET_ABS!" (
    echo skip missing file: !TARGET_ABS!
    exit /b 0
)

call :remove_existing_file_link "!LINK_ABS!"
if errorlevel 1 exit /b 1

for %%I in ("!LINK_ABS!") do if not exist "%%~dpI" mkdir "%%~dpI"
mklink "!LINK_ABS!" "!TARGET_ABS!" >nul 2>nul
if errorlevel 1 (
    mklink /H "!LINK_ABS!" "!TARGET_ABS!" >nul 2>nul
    if errorlevel 1 (
        echo failed to create file link: !LINK_ABS! -^> !TARGET_ABS!
        echo run this script as Administrator, enable Windows Developer Mode, or keep source and link on the same drive for hardlink fallback.
        exit /b 1
    )
    echo create hardlink !LINK_ABS! -^> !TARGET_ABS!
    exit /b 0
)

echo create symlink !LINK_ABS! -^> !TARGET_ABS!
exit /b 0

:link_dir
set "LINK=%~1"
set "TARGET=%~2"
for %%I in ("%TARGET%") do set "TARGET_ABS=%%~fI"
for %%I in ("%LINK%") do set "LINK_ABS=%%~fI"

if not exist "!TARGET_ABS!\" (
    echo skip missing directory: !TARGET_ABS!
    exit /b 0
)

call :remove_existing_link "!LINK_ABS!"
if errorlevel 1 exit /b 1

for %%I in ("!LINK_ABS!") do if not exist "%%~dpI" mkdir "%%~dpI"
mklink /D "!LINK_ABS!" "!TARGET_ABS!" >nul 2>nul
if errorlevel 1 (
    mklink /J "!LINK_ABS!" "!TARGET_ABS!" >nul 2>nul
    if errorlevel 1 (
        echo failed to create directory link: !LINK_ABS! -^> !TARGET_ABS!
        echo run this script as Administrator or enable Windows Developer Mode.
        exit /b 1
    )
    echo create junction !LINK_ABS! -^> !TARGET_ABS!
    exit /b 0
)

echo create symlink !LINK_ABS! -^> !TARGET_ABS!
exit /b 0

:remove_existing_file_link
set "LINK_PATH=%~1"
if not exist "!LINK_PATH!" exit /b 0
if exist "!LINK_PATH!\" (
    echo existing path is a directory, refusing to overwrite as file link: !LINK_PATH!
    exit /b 1
)

del /f /q "!LINK_PATH!"
exit /b %errorlevel%

:remove_existing_link
set "LINK_PATH=%~1"
if not exist "!LINK_PATH!" if not exist "!LINK_PATH!\" exit /b 0

fsutil reparsepoint query "!LINK_PATH!" >nul 2>nul
if errorlevel 1 (
    echo existing path is not a symbolic link, refusing to overwrite: !LINK_PATH!
    exit /b 1
)

if exist "!LINK_PATH!\" (
    rmdir "!LINK_PATH!"
) else (
    del /f /q "!LINK_PATH!"
)
exit /b %errorlevel%

