@echo off
:checkPrivileges
NET FILE 1>NUL 2>NUL
if '%errorlevel%' == '0' (
    echo Running with Administrator privileges.
    REM Your batch commands here
) else (
    echo This script needs to be run with Administrator privileges.
    echo Please re-run this script as an Administrator.
    exit /B
)

python tools\link_dltool.py

if %errorlevel% neq 0 (
    echo "link dltool dll failed"
    exit /b 1
) else (
    echo "link dltool dll success"
)

python tools\link_sqlite.py

if %errorlevel% neq 0 (
    echo "link sqlite3 dll failed"
    exit /b 1
) else (
    echo "link sqlite3 dll success"
)

python tools\link_opencv.py

if %errorlevel% neq 0 (
    echo "link opencv dll failed"
    exit /b 1
) else (
    echo "link opencv dll success"
)

python tools\link_python.py

if %errorlevel% neq 0 (
    echo "link python dll failed"
    exit /b 1
) else (
    echo "link python dll success"
)


