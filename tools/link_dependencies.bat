@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

rem DeepLearningTool dependency link script for Windows.
rem Runtime dependency names are listed in tools\dependencies.

pushd "%~dp0.." || exit /b 1

call :link_dltool
if errorlevel 1 exit /b 1
echo link dltool dll success

call :link_deps
if errorlevel 1 exit /b 1
echo link external dependencies success

call :link_test
if errorlevel 1 exit /b 1
echo link test success

popd
exit /b 0

:link_dltool
call :link_dir "build\bin\dltool" "build\dltool"
if errorlevel 1 exit /b 1

if exist "build\bin\dltool\" (
    for /r "build\bin\dltool" %%F in (dltool_*.dll) do (
        call :link_file "%%~fF" "build\bin\%%~nxF"
        if errorlevel 1 exit /b 1
    )
) else (
    echo skip dltool module dll links, missing build\bin\dltool
)
exit /b 0

:link_deps
set "DEPENDENCIES_FILE=tools\dependencies"
if not exist "!DEPENDENCIES_FILE!" (
    echo skip external dependency links, missing !DEPENDENCIES_FILE!
    exit /b 0
)

set "DEPENDENCY_SECTION="
set "DEPENDENCY_CMAKE="
set "DEPENDENCY_ROOT_VAR="
set "DEPENDENCY_ROOT="
set "DEPENDENCY_DESTS="
set "DEPENDENCY_ENABLED=1"

for /f "usebackq tokens=* delims=" %%L in ("!DEPENDENCIES_FILE!") do (
    call :proc_dep_line "%%L"
    if errorlevel 1 exit /b 1
)
exit /b 0

:proc_dep_line
set "LINE=%~1"
call :trim LINE
if not defined LINE exit /b 0
if "!LINE:~0,1!"=="#" exit /b 0
if "!LINE:~0,1!"==";" exit /b 0

if "!LINE:~0,1!"=="[" (
    set "DEPENDENCY_SECTION=!LINE:~1,-1!"
    call :trim DEPENDENCY_SECTION
    set "DEPENDENCY_CMAKE="
    set "DEPENDENCY_ROOT_VAR="
    set "DEPENDENCY_ROOT="
    set "DEPENDENCY_DESTS="
    set "DEPENDENCY_ENABLED=1"
    exit /b 0
)

set "KEY="
set "VALUE="
for /f "tokens=1* delims==" %%A in ("!LINE!") do (
    set "KEY=%%A"
    set "VALUE=%%B"
)
call :trim KEY
call :trim VALUE
if not defined KEY exit /b 0

if /i "!KEY!"=="cmake" (
    set "DEPENDENCY_CMAKE=!VALUE:/=\!"
    exit /b 0
)

if /i "!KEY!"=="root" (
    set "DEPENDENCY_ROOT_SPEC=!VALUE!"
    set "DEPENDENCY_ROOT_VAR=!VALUE!"
    set "DEPENDENCY_ROOT="

    call :is_direct_root "!DEPENDENCY_ROOT_SPEC!"
    if not errorlevel 1 (
        set "DEPENDENCY_ROOT=!DEPENDENCY_ROOT_SPEC!"
        set "DEPENDENCY_ROOT=!DEPENDENCY_ROOT:/=\!"
        set "DEPENDENCY_ENABLED=1"
        exit /b 0
    )

    if not defined DEPENDENCY_CMAKE (
        set "DEPENDENCY_ROOT=!DEPENDENCY_ROOT_SPEC!"
        set "DEPENDENCY_ROOT=!DEPENDENCY_ROOT:/=\!"
        set "DEPENDENCY_ENABLED=1"
        exit /b 0
    )

    call :read_cmake_quoted_value "!DEPENDENCY_CMAKE!" "!DEPENDENCY_ROOT_VAR!" DEPENDENCY_ROOT
    if not defined DEPENDENCY_ROOT (
        echo skip dependency !DEPENDENCY_SECTION!, !DEPENDENCY_ROOT_VAR! was not found in !DEPENDENCY_CMAKE!
        set "DEPENDENCY_ENABLED=0"
        exit /b 0
    )

    set "DEPENDENCY_ENABLED=1"
    exit /b 0
)

if /i "!KEY!"=="dest" (
    set "DEST_VALUE=!VALUE:/=\!"
    if defined DEPENDENCY_DESTS (
        set "DEPENDENCY_DESTS=!DEPENDENCY_DESTS! !DEST_VALUE!"
    ) else (
        set "DEPENDENCY_DESTS=!DEST_VALUE!"
    )
    exit /b 0
)

if /i "!KEY!"=="windows" (
    call :link_dep_pattern "!VALUE!"
    exit /b %errorlevel%
)

if /i "!KEY!"=="all" (
    call :link_dep_pattern "!VALUE!"
    exit /b %errorlevel%
)

exit /b 0

:is_direct_root
set "ROOT_SPEC=%~1"
if not "!ROOT_SPEC::=!"=="!ROOT_SPEC!" exit /b 0
if not "!ROOT_SPEC:/=!"=="!ROOT_SPEC!" exit /b 0
if not "!ROOT_SPEC:\=!"=="!ROOT_SPEC!" exit /b 0
if "!ROOT_SPEC:~0,1!"=="." exit /b 0
if "!ROOT_SPEC:~0,1!"=="~" exit /b 0
exit /b 1

:link_dep_pattern
if "%~1"=="" exit /b 0
if "!DEPENDENCY_ENABLED!"=="0" exit /b 0

if not defined DEPENDENCY_ROOT (
    echo skip dependency !DEPENDENCY_SECTION! pattern %~1, root was not configured
    exit /b 0
)

if not defined DEPENDENCY_DESTS (
    echo skip dependency !DEPENDENCY_SECTION! pattern %~1, destination was not configured
    exit /b 0
)

set "REL_PATTERN=%~1"
set "REL_PATTERN=!REL_PATTERN:/=\!"
set "ROOT_PATH=!DEPENDENCY_ROOT:/=\!"
set "FULL_PATTERN=!ROOT_PATH!\!REL_PATTERN!"

set "WILDCARD_PATTERN=0"
echo(!FULL_PATTERN! | findstr /l /c:"*" /c:"?" >nul
if not errorlevel 1 set "WILDCARD_PATTERN=1"

if "!WILDCARD_PATTERN!"=="0" (
    call :link_dep_file "!FULL_PATTERN!"
    exit /b %errorlevel%
)

set "MATCHED_ANY=0"
call :split_pattern_path "!ROOT_PATH!" "!REL_PATTERN!"

if not exist "!PATTERN_DIR!" exit /b 0

for /f "delims=" %%F in ('dir /b /a-d "!PATTERN_DIR!!PATTERN_NAME!" 2^>nul') do (
    set "MATCHED_ANY=1"
    set "MATCHED_FILE=!PATTERN_DIR!%%F"
    call :link_dep_file "!MATCHED_FILE!"
    if errorlevel 1 exit /b 1
)

if "!MATCHED_ANY!"=="0" exit /b 0
exit /b 0

:split_pattern_path
set "PATTERN_DIR=%~1\"
set "PATTERN_NAME=%~2"

:split_pattern_loop
for /f "tokens=1* delims=\" %%A in ("!PATTERN_NAME!") do (
    if "%%B"=="" (
        set "PATTERN_NAME=%%A"
        exit /b 0
    )

    set "PATTERN_DIR=!PATTERN_DIR!%%A\"
    set "PATTERN_NAME=%%B"
    goto split_pattern_loop
)
exit /b 0

:link_dep_file
set "TARGET=%~1"
for %%I in ("!TARGET!") do set "TARGET_NAME=%%~nxI"
for %%D in (!DEPENDENCY_DESTS!) do (
    set "DEST_PATH=%%~D"
    call :link_file "!TARGET!" "!DEST_PATH!\!TARGET_NAME!"
    if errorlevel 1 exit /b 1
)
exit /b 0

:link_test
if exist "build\tests\" (
    call :link_dir "build\tests\dltool" "build\dltool"
    exit /b %errorlevel%
)

echo skip test link, missing build\tests
exit /b 0

:read_cmake_quoted_value
set "CMAKE_FILE=%~1"
set "CMAKE_KEY=%~2"
set "CMAKE_OUT=%~3"

if not exist "!CMAKE_FILE!" exit /b 0

for /f tokens^=1^,2^ delims^=^" %%A in (!CMAKE_FILE!) do (
    set "CMAKE_PREFIX=%%A"
    if not "!CMAKE_PREFIX:%CMAKE_KEY%=!"=="!CMAKE_PREFIX!" (
        set "%CMAKE_OUT%=%%B"
    )
)
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

:trim
setlocal EnableDelayedExpansion
set "TRIM_VALUE=!%~1!"
:trim_left
if defined TRIM_VALUE if "!TRIM_VALUE:~0,1!"==" " (
    set "TRIM_VALUE=!TRIM_VALUE:~1!"
    goto trim_left
)
:trim_right
if defined TRIM_VALUE if "!TRIM_VALUE:~-1!"==" " (
    set "TRIM_VALUE=!TRIM_VALUE:~0,-1!"
    goto trim_right
)
endlocal & set "%~1=%TRIM_VALUE%"
exit /b 0
