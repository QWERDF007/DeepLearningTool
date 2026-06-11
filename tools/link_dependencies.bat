@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

REM DeepLearningTool Windows 依赖链接脚本。
REM 这里不依赖 Python，直接使用 mklink 创建构建目录中的链接。
REM 目录链接优先使用符号链接，失败后回退到 junction；文件链接优先符号链接，失败后回退到 hardlink。

REM 切换到项目根目录，保证从任意当前目录调用脚本时，相对路径都按项目根目录解析。
pushd "%~dp0.." || exit /b 1

REM 链接项目自身模块目录，并把模块 DLL 暴露到 build\bin，方便可执行程序直接加载。
call :link_dltool
if errorlevel 1 exit /b 1
echo link dltool dll success

REM 根据 cmake\ConfigSQLite.cmake 中的 CMAKE_PREFIX_PATH 链接 sqlite3.dll。
call :link_sqlite
if errorlevel 1 exit /b 1
echo link sqlite3 dll success

call :link_inferrt
if errorlevel 1 exit /b 1
echo link inferrt runtime dll success

REM 如果已经构建测试目标，则给 build\tests 创建同样的 dltool 模块目录链接。
call :link_test
if errorlevel 1 exit /b 1
echo link test success

popd
exit /b 0

:link_dltool
REM build\bin\dltool 指向 build\dltool，使运行目录可以找到项目 QML/插件模块。
call :link_dir "build\bin\dltool" "build\dltool"
if errorlevel 1 exit /b 1

REM 将 build\bin\dltool 下的项目 DLL 链接到 build\bin 根目录，匹配 Windows DLL 搜索规则。
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
REM 从 CMake 配置读取 SQLite 安装根目录，当前约定 sqlite3.dll 位于 <root>\lib。
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

:link_inferrt
set "INFERRT_ROOT="
set "INFERRT_DEBUG_ROOT="
if exist "cmake\ConfigInferRT.cmake" (
    for /f tokens^=2^ delims^=^" %%A in ('findstr /b /c:"set(INFERRT_ROOT " "cmake\ConfigInferRT.cmake"') do (
        set "INFERRT_ROOT=%%A"
    )
    for /f tokens^=2^ delims^=^" %%A in ('findstr /b /c:"set(INFERRT_DEBUG_ROOT " "cmake\ConfigInferRT.cmake"') do (
        set "INFERRT_DEBUG_ROOT=%%A"
    )
)

if not defined INFERRT_ROOT (
    echo skip InferRT runtime links, INFERRT_ROOT was not found in cmake\ConfigInferRT.cmake
    exit /b 0
)

set "INFERRT_BIN=!INFERRT_ROOT:/=\!\bin"
set "INFERRT_DEBUG_BIN=!INFERRT_DEBUG_ROOT:/=\!\bin"

for %%P in (
    "!INFERRT_BIN!\libiomp5md.dll"
    "!INFERRT_BIN!\mkl_*.dll"
    "!INFERRT_BIN!\nvinfer_*.dll"
    "!INFERRT_BIN!\nvonnxparser_*.dll"
    "!INFERRT_BIN!\cudnn*.dll"
    "!INFERRT_BIN!\cublas*.dll"
    "!INFERRT_BIN!\cufft*.dll"
    "!INFERRT_BIN!\onnxruntime*.dll"
    "!INFERRT_BIN!\faiss.dll"
    "!INFERRT_BIN!\inferrt_core.dll"
    "!INFERRT_BIN!\inferrt_cvcuda.dll"
    "!INFERRT_BIN!\inferrt_features.dll"
    "!INFERRT_BIN!\inferrt_model.dll"
    "!INFERRT_BIN!\inferrt_util.dll"
    "!INFERRT_BIN!\opencv_world480.dll"
    "!INFERRT_BIN!\faissd.dll"
    "!INFERRT_DEBUG_BIN!\inferrt_cored.dll"
    "!INFERRT_DEBUG_BIN!\inferrt_cvcudad.dll"
    "!INFERRT_DEBUG_BIN!\inferrt_featuresd.dll"
    "!INFERRT_DEBUG_BIN!\inferrt_modeld.dll"
    "!INFERRT_DEBUG_BIN!\inferrt_utild.dll"
    "!INFERRT_DEBUG_BIN!\opencv_world480d.dll"
) do (
    call :link_inferrt_pattern "%%~P"
    if errorlevel 1 exit /b 1
)

exit /b 0

:link_inferrt_pattern
for %%F in (%~1) do (
    call :link_inferrt_file "%%~fF"
    if errorlevel 1 exit /b 1
)
exit /b 0

:link_inferrt_file
set "TARGET=%~1"
call :link_file "!TARGET!" "build\bin\%~nx1"
if errorlevel 1 exit /b 1
call :link_file "!TARGET!" "build\dltool\data\%~nx1"
if errorlevel 1 exit /b 1
if exist "build\tests\" (
    call :link_file "!TARGET!" "build\tests\%~nx1"
    if errorlevel 1 exit /b 1
)
exit /b 0

:link_test
REM 测试可执行程序运行目录不同，需要额外链接 dltool 模块目录。
if exist "build\tests\" (
    call :link_dir "build\tests\dltool" "build\dltool"
    exit /b %errorlevel%
)

echo skip test link, missing build\tests
exit /b 0

:link_file
REM 创建文件链接。目标不存在时只跳过，保留原 Python 脚本的宽松行为。
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

REM 普通用户环境可能没有创建符号链接权限，所以失败后尝试 hardlink。
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
REM 创建目录链接。目标不存在时只跳过，避免未构建某些目标时脚本失败。
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

REM 目录符号链接需要权限；junction 通常不需要管理员权限，适合作为回退。
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
REM 文件链接可能是 symlink 或 hardlink。这里仅处理文件路径，遇到目录则拒绝覆盖。
set "LINK_PATH=%~1"
if not exist "!LINK_PATH!" exit /b 0
if exist "!LINK_PATH!\" (
    echo existing path is a directory, refusing to overwrite as file link: !LINK_PATH!
    exit /b 1
)

del /f /q "!LINK_PATH!"
exit /b %errorlevel%

:remove_existing_link
REM 删除旧目录链接前先确认它是 reparse point，避免误删真实目录。
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

