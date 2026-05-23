# DeepLearningTool Windows 打包脚本。
# 目标：把 build 目录中的可执行程序、本项目动态库、QML 模块、Qt 运行时和必要的系统运行库复制到 install 目录。
# 运行入口通常使用 tools\package_app.bat，也可以直接用 PowerShell 调用本脚本。
#
# 参数说明：
# - BuildDir：CMake 构建目录，默认使用项目根目录下的 build。
# - InstallDir：打包输出目录，默认使用项目根目录下的 install。
# - Config：打包配置；auto 会根据 CMakeCache 和 DLL 命名自动判断 debug/release。
# - WinDeployQt：可显式传入 windeployqt.exe 路径；为空时自动查找。
# - SkipWinDeployQt：跳过 Qt 依赖部署，仅复制项目自身产物。
# - IncludePdb：是否复制 PDB 调试符号。默认不复制，避免 install 体积过大。
# - IncludeQmlModuleDir：额外复制 build/dltool 目录。当前 QML 已嵌入资源，默认不需要。
# - NoClean：不清理 install 目录，直接覆盖复制。
# - ForceClean：强制清理 install 目录。用于清理不是本脚本生成的旧目录。
param(
    [string]$BuildDir = "build",
    [string]$InstallDir = "install",
    [ValidateSet("auto", "debug", "release")]
    [string]$Config = "auto",
    [string]$WinDeployQt = "",
    [switch]$SkipWinDeployQt,
    [switch]$IncludePdb,
    [switch]$IncludeQmlModuleDir,
    [switch]$NoClean,
    [switch]$ForceClean
)

# 出错时立即终止，避免生成半失败的安装目录还误以为成功。
$ErrorActionPreference = "Stop"

$ProjectName = "dltool"

# 标记 install 是本脚本生成的。后续默认清理时，只自动清理带有该标记的目录。
$MarkerFile = ".dltool_package"
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path

# 将相对路径解析到项目根目录下，方便从任意当前目录调用脚本。
function Resolve-ProjectPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

# 从 CMakeCache.txt 中读取形如 KEY:TYPE=VALUE 或 KEY=VALUE 的缓存项。
function Read-CMakeCacheValue {
    param(
        [string]$CacheFile,
        [string]$Key
    )

    if (-not (Test-Path -LiteralPath $CacheFile)) {
        return $null
    }

    $pattern = "^$([regex]::Escape($Key))(?::[^=]+)?=(.*)$"
    foreach ($line in Get-Content -LiteralPath $CacheFile -Encoding UTF8) {
        $match = [regex]::Match($line.Trim(), $pattern)
        if ($match.Success) {
            return $match.Groups[1].Value.Trim()
        }
    }
    return $null
}

# 从简单 CMake set(NAME "VALUE") 语句中读取路径配置。
# 当前用于读取 cmake/ConfigQT.cmake 和 cmake/ConfigSQLite.cmake 中的本机安装路径。
function Read-CMakeSet {
    param(
        [string]$CMakeFile,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $CMakeFile)) {
        return $null
    }

    $pattern = "^\s*set\s*\(\s*$([regex]::Escape($Name))\s+`"([^`"]+)`"\s*\)"
    foreach ($line in Get-Content -LiteralPath $CMakeFile -Encoding UTF8) {
        $match = [regex]::Match($line, $pattern)
        if ($match.Success) {
            return $match.Groups[1].Value.Trim()
        }
    }
    return $null
}

# 复制单个文件。
# 如果源文件是符号链接，则复制链接目标本身，确保 install 目录不依赖 build 里的软链接。
function Copy-PackageFile {
    param(
        [string]$Source,
        [string]$Destination
    )

    $item = Get-Item -LiteralPath $Source -Force
    $copySource = $item.FullName
    if ($item.LinkType -and $item.Target) {
        $copySource = (Resolve-Path -LiteralPath $item.Target).Path
    }

    $parent = Split-Path -Parent $Destination
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }

    Copy-Item -LiteralPath $copySource -Destination $Destination -Force
    Write-Host "copy $copySource -> $Destination"
}

# 判断项目 DLL 是否属于当前打包配置。
# MSVC Debug 产物带 d 后缀，例如 dltool_datad.dll；Release 产物没有该后缀。
function Test-ProjectDllMatchesConfig {
    param(
        [string]$Path,
        [string]$DetectedConfig
    )

    $name = [System.IO.Path]::GetFileName($Path).ToLowerInvariant()
    $prefix = [regex]::Escape($ProjectName)

    if ($name -notmatch "^${prefix}_.+\.dll$") {
        return $true
    }

    $isDebugDll = $name -match "^${prefix}_.+d\.dll$"
    if ($DetectedConfig -eq "debug") {
        return $isDebugDll
    }
    return -not $isDebugDll
}

# 清理或创建 install 目录。
# 默认只自动清理带 .dltool_package 标记的目录，避免误删用户手工放入 install 的内容。
function Clear-InstallDirectory {
    param(
        [string]$Path,
        [bool]$Clean,
        [bool]$Force
    )

    if (-not $Clean) {
        if (-not (Test-Path -LiteralPath $Path)) {
            New-Item -ItemType Directory -Path $Path | Out-Null
        }
        return
    }

    $marker = Join-Path $Path $MarkerFile
    if ((Test-Path -LiteralPath $Path) -and
        @(Get-ChildItem -LiteralPath $Path -Force).Count -gt 0 -and
        -not (Test-Path -LiteralPath $marker) -and
        -not $Force) {
        Write-Host "install dir exists and has no $MarkerFile; skip cleaning: $Path"
        Write-Host "pass -ForceClean to remove existing contents before packaging"
        return
    }

    if (Test-Path -LiteralPath $Path) {
        foreach ($child in Get-ChildItem -LiteralPath $Path -Force) {
            Remove-Item -LiteralPath $child.FullName -Recurse -Force
        }
    }
    else {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
}

# 查找 dltool.exe。兼容当前项目的 build/bin 布局和部分多配置生成器布局。
function Find-Executable {
    param(
        [string]$BuildPath,
        [string]$ConfigName
    )

    $candidates = @(
        (Join-Path $BuildPath "bin\$ProjectName.exe"),
        (Join-Path $BuildPath "bin\$ConfigName\$ProjectName.exe"),
        (Join-Path $BuildPath "$ConfigName\bin\$ProjectName.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $match = Get-ChildItem -LiteralPath $BuildPath -Recurse -Filter "$ProjectName.exe" -File |
        Sort-Object { $_.FullName.Length } |
        Select-Object -First 1
    if ($match) {
        return $match.FullName
    }

    throw "cannot find $ProjectName.exe under $BuildPath"
}

# 判断打包 debug 还是 release。
# Visual Studio 多配置构建通常 CMAKE_BUILD_TYPE 为空，所以还会根据 debug DLL 的 d 后缀做兜底判断。
function Get-DetectedConfig {
    param(
        [string]$BuildPath,
        [string]$Requested
    )

    if ($Requested -eq "debug" -or $Requested -eq "release") {
        return $Requested
    }

    $buildType = Read-CMakeCacheValue -CacheFile (Join-Path $BuildPath "CMakeCache.txt") -Key "CMAKE_BUILD_TYPE"
    if ($buildType -and $buildType.ToLowerInvariant() -eq "debug") {
        return "debug"
    }
    if ($buildType -and @("release", "relwithdebinfo", "minsizerel") -contains $buildType.ToLowerInvariant()) {
        return "release"
    }

    $moduleRoot = Join-Path $BuildPath $ProjectName
    if ((Test-Path -LiteralPath $moduleRoot) -and
        (Get-ChildItem -LiteralPath $moduleRoot -Recurse -Filter "${ProjectName}_*d.dll" -File | Select-Object -First 1)) {
        return "debug"
    }

    return "release"
}

# 判断目标架构，用于选择 MSVC 运行库和 Windows SDK DLL 的 x64/x86/arm64 目录。
function Get-TargetArchitecture {
    param([string]$BuildPath)

    $platform = Read-CMakeCacheValue -CacheFile (Join-Path $BuildPath "CMakeCache.txt") -Key "CMAKE_GENERATOR_PLATFORM"
    if ($platform) {
        switch ($platform.ToLowerInvariant()) {
            "x64" { return "x64" }
            "win32" { return "x86" }
            "x86" { return "x86" }
            "arm64" { return "arm64" }
        }
    }

    switch ($env:PROCESSOR_ARCHITECTURE) {
        "AMD64" { return "x64" }
        "ARM64" { return "arm64" }
        default { return "x86" }
    }
}

# 查找 Visual Studio 安装目录。
# 优先使用当前开发者命令行的 VCINSTALLDIR，其次使用 vswhere，再兜底扫描常见 VS 2022 安装路径。
function Get-VisualStudioInstallRoots {
    $roots = New-Object System.Collections.Generic.List[string]

    if ($env:VCINSTALLDIR) {
        $vcInstallDir = ($env:VCINSTALLDIR).TrimEnd([char]"\")
        $vcRoot = Split-Path -Parent $vcInstallDir
        if ($vcRoot) {
            $roots.Add($vcRoot)
        }
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($path) {
            $roots.Add($path.Trim())
        }
    }

    foreach ($path in @(
            "C:\Program Files\Microsoft Visual Studio\2022\Community",
            "C:\Program Files\Microsoft Visual Studio\2022\Professional",
            "C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
            "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
        )) {
        if (Test-Path -LiteralPath $path) {
            $roots.Add($path)
        }
    }

    return $roots | Select-Object -Unique
}

# 在版本号命名的目录中取最新版本，例如 MSVC Redist 或 Windows Kits 版本目录。
function Get-LatestVersionDirectory {
    param([string]$Root)

    if (-not (Test-Path -LiteralPath $Root)) {
        return $null
    }

    return Get-ChildItem -LiteralPath $Root -Directory |
        Sort-Object {
            try { [version]$_.Name }
            catch { [version]"0.0" }
        } -Descending |
        Select-Object -First 1
}

# 复制 MSVC 运行库。
# Debug 构建需要 debug_nonredist 中的 *d.dll，Release 构建使用可再分发 CRT。
function Copy-MsvcRuntime {
    param(
        [string]$OutputPath,
        [string]$DetectedConfig,
        [string]$Architecture
    )

    foreach ($vsRoot in Get-VisualStudioInstallRoots) {
        $redistRoot = Join-Path $vsRoot "VC\Redist\MSVC"
        $versionDir = Get-LatestVersionDirectory -Root $redistRoot
        if (-not $versionDir) {
            continue
        }

        $runtimeDir = if ($DetectedConfig -eq "debug") {
            Join-Path $versionDir.FullName "debug_nonredist\$Architecture"
        }
        else {
            Join-Path $versionDir.FullName "$Architecture"
        }

        if (-not (Test-Path -LiteralPath $runtimeDir)) {
            continue
        }

        foreach ($dll in Get-ChildItem -LiteralPath $runtimeDir -Recurse -Filter "*.dll" -File) {
            Copy-PackageFile -Source $dll.FullName -Destination (Join-Path $OutputPath $dll.Name)
        }
        return
    }

    Write-Warning "MSVC runtime DLLs were not found"
}

# 获取最新 Windows SDK bin 目录。
function Get-WindowsKitBinRoot {
    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    $versionDir = Get-LatestVersionDirectory -Root $kitsRoot
    if ($versionDir) {
        return $versionDir.FullName
    }
    return $null
}

# 复制 Windows SDK 运行时依赖。
# dxcompiler/dxil 用于 Qt 图形后端；Debug 下额外复制 ucrtbased.dll。
function Copy-WindowsSdkDependencies {
    param(
        [string]$OutputPath,
        [string]$DetectedConfig,
        [string]$Architecture
    )

    $kitBin = Get-WindowsKitBinRoot
    if (-not $kitBin) {
        Write-Warning "Windows SDK bin directory was not found"
        return
    }

    foreach ($name in @("dxcompiler.dll", "dxil.dll")) {
        $candidate = Join-Path $kitBin "$Architecture\$name"
        if (Test-Path -LiteralPath $candidate) {
            Copy-PackageFile -Source $candidate -Destination (Join-Path $OutputPath $name)
        }
    }

    if ($DetectedConfig -eq "debug") {
        $ucrt = Join-Path $kitBin "$Architecture\ucrt\ucrtbased.dll"
        if (Test-Path -LiteralPath $ucrt) {
            Copy-PackageFile -Source $ucrt -Destination (Join-Path $OutputPath "ucrtbased.dll")
        }
        else {
            Write-Warning "ucrtbased.dll was not found"
        }
    }
}

# 查找 Qt 根目录。
# 来源包括环境变量、cmake/ConfigQT.cmake 和 build/CMakeCache.txt。
function Get-QtRoots {
    param([string]$BuildPath)

    $roots = New-Object System.Collections.Generic.List[string]

    foreach ($name in @("Qt6_ROOT", "QT6_ROOT", "Qt_ROOT", "QT_ROOT", "QTDIR")) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value) {
            $roots.Add($value)
        }
    }

    $configRoot = Read-CMakeSet -CMakeFile (Join-Path $RepoRoot "cmake\ConfigQT.cmake") -Name "Qt6_ROOT"
    if ($configRoot) {
        $roots.Add($configRoot)
    }

    $cacheFile = Join-Path $BuildPath "CMakeCache.txt"
    foreach ($key in @("Qt6Core_DIR", "Qt6_DIR")) {
        $value = Read-CMakeCacheValue -CacheFile $cacheFile -Key $key
        if (-not $value) {
            continue
        }

        $normalized = $value.Replace("/", "\")
        $index = $normalized.ToLowerInvariant().IndexOf("\lib\")
        if ($index -gt 0) {
            $roots.Add($normalized.Substring(0, $index))
        }
    }

    return $roots | Select-Object -Unique
}

# 查找 windeployqt.exe。
# windeployqt 负责复制 Qt DLL、platforms、imageformats、qml、translations 等运行时文件。
function Find-WinDeployQt {
    param(
        [string]$BuildPath,
        [string]$ExplicitPath
    )

    if ($ExplicitPath) {
        if (Test-Path -LiteralPath $ExplicitPath) {
            return (Resolve-Path -LiteralPath $ExplicitPath).Path
        }
        return $null
    }

    $command = Get-Command "windeployqt.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    foreach ($qtRoot in Get-QtRoots -BuildPath $BuildPath) {
        $candidate = Join-Path $qtRoot "bin\windeployqt.exe"
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

# 递归复制目录，同时过滤编译中间产物。
# QML 文件、qmldir、qmltypes 和 DLL 会被保留；lib/exp/pdb 等按参数过滤。
function Copy-DirectoryFiltered {
    param(
        [string]$Source,
        [string]$Destination,
        [bool]$CopyPdb,
        [string]$DetectedConfig
    )

    $sourceRoot = (Resolve-Path -LiteralPath $Source).Path.TrimEnd("\")
    if (-not (Test-Path -LiteralPath $Destination)) {
        New-Item -ItemType Directory -Path $Destination | Out-Null
    }

    $ignored = New-Object System.Collections.Generic.HashSet[string]
    foreach ($extension in @(".exp", ".lib", ".ilk", ".obj")) {
        [void]$ignored.Add($extension)
    }
    if (-not $CopyPdb) {
        [void]$ignored.Add(".pdb")
    }

    foreach ($item in Get-ChildItem -LiteralPath $Source -Recurse -Force) {
        $relative = $item.FullName.Substring($sourceRoot.Length).TrimStart("\")
        $target = Join-Path $Destination $relative

        if ($item.PSIsContainer) {
            if (-not (Test-Path -LiteralPath $target)) {
                New-Item -ItemType Directory -Path $target | Out-Null
            }
            continue
        }

        if ($ignored.Contains($item.Extension.ToLowerInvariant())) {
            continue
        }
        if ($item.Extension.ToLowerInvariant() -eq ".dll" -and
            -not (Test-ProjectDllMatchesConfig -Path $item.FullName -DetectedConfig $DetectedConfig)) {
            continue
        }

        Copy-PackageFile -Source $item.FullName -Destination $target
    }

    Write-Host "copy $Source -> $Destination"
}

# 复制项目自身运行时。
# 默认只复制各模块 DLL 到 install 根目录，方便 Windows DLL 搜索。
# build/dltool 下的 QML 模块目录通常已通过 Qt 资源嵌入，只有调试散装 QML 时才需要额外复制。
function Copy-ProjectRuntime {
    param(
        [string]$BuildPath,
        [string]$OutputPath,
        [bool]$CopyPdb,
        [string]$DetectedConfig,
        [bool]$CopyQmlModuleDir
    )

    $moduleRoot = Join-Path $BuildPath $ProjectName
    if (-not (Test-Path -LiteralPath $moduleRoot)) {
        throw "cannot find QML module output: $moduleRoot"
    }

    if ($CopyQmlModuleDir) {
        Copy-DirectoryFiltered -Source $moduleRoot -Destination (Join-Path $OutputPath $ProjectName) -CopyPdb $CopyPdb -DetectedConfig $DetectedConfig
    }

    foreach ($dll in Get-ChildItem -LiteralPath $moduleRoot -Recurse -Filter "*.dll" -File) {
        if (-not (Test-ProjectDllMatchesConfig -Path $dll.FullName -DetectedConfig $DetectedConfig)) {
            continue
        }
        Copy-PackageFile -Source $dll.FullName -Destination (Join-Path $OutputPath $dll.Name)
    }

    $binDir = Join-Path $BuildPath "bin"
    if (Test-Path -LiteralPath $binDir) {
        foreach ($dll in Get-ChildItem -LiteralPath $binDir -Filter "*.dll" -File) {
            if (-not (Test-ProjectDllMatchesConfig -Path $dll.FullName -DetectedConfig $DetectedConfig)) {
                continue
            }
            Copy-PackageFile -Source $dll.FullName -Destination (Join-Path $OutputPath $dll.Name)
        }
        if ($CopyPdb) {
            foreach ($pdb in Get-ChildItem -LiteralPath $binDir -Filter "*.pdb" -File) {
                Copy-PackageFile -Source $pdb.FullName -Destination (Join-Path $OutputPath $pdb.Name)
            }
        }
    }
}

# 复制 sqlite3.dll。
# 优先使用 CMakeCache 中 SQLite3_LIBRARY 对应目录，兜底使用 cmake/ConfigSQLite.cmake 中的路径。
function Copy-SqliteIfNeeded {
    param(
        [string]$BuildPath,
        [string]$OutputPath
    )

    $target = Join-Path $OutputPath "sqlite3.dll"
    if (Test-Path -LiteralPath $target) {
        return
    }

    $candidates = New-Object System.Collections.Generic.List[string]

    $sqliteLib = Read-CMakeCacheValue -CacheFile (Join-Path $BuildPath "CMakeCache.txt") -Key "SQLite3_LIBRARY"
    if ($sqliteLib) {
        $candidates.Add((Join-Path (Split-Path -Parent $sqliteLib) "sqlite3.dll"))
    }

    $sqliteRoot = Read-CMakeSet -CMakeFile (Join-Path $RepoRoot "cmake\ConfigSQLite.cmake") -Name "CMAKE_PREFIX_PATH"
    if ($sqliteRoot) {
        $candidates.Add((Join-Path $sqliteRoot "lib\sqlite3.dll"))
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            Copy-PackageFile -Source $candidate -Destination $target
            return
        }
    }

    Write-Warning "sqlite3.dll was not found"
}

# 调用 windeployqt 部署 Qt 依赖。
# --qmldir 指向 build/dltool，使 Qt 能扫描本项目生成的 QML 模块导入关系。
function Invoke-WinDeployQt {
    param(
        [string]$BuildPath,
        [string]$OutputPath,
        [string]$ExePath,
        [string]$DetectedConfig,
        [string]$ExplicitPath,
        [bool]$Skip
    )

    if ($Skip) {
        Write-Host "skip windeployqt"
        return
    }

    $windeployqt = Find-WinDeployQt -BuildPath $BuildPath -ExplicitPath $ExplicitPath
    if (-not $windeployqt) {
        throw "cannot find windeployqt; pass -WinDeployQt <path>"
    }

    $qmlDir = Join-Path $BuildPath $ProjectName
    $configArg = "--$DetectedConfig"
    $args = @("--dir", $OutputPath, "--qmldir", $qmlDir, $configArg, $ExePath)

    Write-Host "run `"$windeployqt`" $($args -join ' ')"
    & $windeployqt @args
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }
}

# 解析输入路径并检查 build 目录。
$ResolvedBuildDir = Resolve-ProjectPath $BuildDir
$ResolvedInstallDir = Resolve-ProjectPath $InstallDir

if (-not (Test-Path -LiteralPath $ResolvedBuildDir)) {
    throw "build dir does not exist: $ResolvedBuildDir"
}

# 根据构建产物判断配置和架构，然后定位待打包 exe。
$DetectedConfig = Get-DetectedConfig -BuildPath $ResolvedBuildDir -Requested $Config
$Architecture = Get-TargetArchitecture -BuildPath $ResolvedBuildDir
$ConfigDirName = if ($DetectedConfig -eq "debug") { "Debug" } else { "Release" }
$SourceExe = Find-Executable -BuildPath $ResolvedBuildDir -ConfigName $ConfigDirName

# 生成 install 目录内容。
Clear-InstallDirectory -Path $ResolvedInstallDir -Clean (-not $NoClean.IsPresent) -Force $ForceClean.IsPresent

$PackagedExe = Join-Path $ResolvedInstallDir (Split-Path -Leaf $SourceExe)
Copy-PackageFile -Source $SourceExe -Destination $PackagedExe
Copy-ProjectRuntime -BuildPath $ResolvedBuildDir -OutputPath $ResolvedInstallDir -CopyPdb $IncludePdb.IsPresent -DetectedConfig $DetectedConfig -CopyQmlModuleDir $IncludeQmlModuleDir.IsPresent
Copy-SqliteIfNeeded -BuildPath $ResolvedBuildDir -OutputPath $ResolvedInstallDir

# 补充 windeployqt 不一定能在普通 PowerShell 中找到的 MSVC/Windows SDK 依赖。
Copy-MsvcRuntime -OutputPath $ResolvedInstallDir -DetectedConfig $DetectedConfig -Architecture $Architecture
Copy-WindowsSdkDependencies -OutputPath $ResolvedInstallDir -DetectedConfig $DetectedConfig -Architecture $Architecture

# 最后部署 Qt 依赖。放在项目 DLL 复制之后，便于 windeployqt 分析 exe 和本项目 QML 模块。
Invoke-WinDeployQt `
    -BuildPath $ResolvedBuildDir `
    -OutputPath $ResolvedInstallDir `
    -ExePath $PackagedExe `
    -DetectedConfig $DetectedConfig `
    -ExplicitPath $WinDeployQt `
    -Skip $SkipWinDeployQt.IsPresent

# 写入打包标记，既记录来源，也用于下次安全清理 install 目录。
$marker = Join-Path $ResolvedInstallDir $MarkerFile
@(
    "generated_by=tools/package_app.ps1",
    "build_dir=$ResolvedBuildDir",
    "config=$DetectedConfig",
    "architecture=$Architecture",
    "include_qml_module_dir=$($IncludeQmlModuleDir.IsPresent)"
) | Set-Content -LiteralPath $marker -Encoding UTF8

Write-Host ""
Write-Host "package complete: $ResolvedInstallDir"
Write-Host "double-click to run: $PackagedExe"
