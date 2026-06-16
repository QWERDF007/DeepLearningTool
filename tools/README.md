# 工具脚本说明

本目录存放 DeepLearningTool 的运行时依赖链接和发布包生成脚本。旧的 `.bat`、`.sh`、`.ps1` 入口已经移除，用户直接通过 Python 脚本执行。

Windows 环境默认使用：

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' --version
```

脚本依赖 `PyYAML` 读取 `tools/dependencies.yaml`。

## `package_app.py`

`package_app.py` 用于生成 release 发布包，默认从 `build/` 读取构建产物，输出到 `install/`。

它会执行这些步骤：

- 复制 `dltool.exe` 或 `dltool` 可执行文件。
- 从 `build/dltool/<module>/` 复制项目 DLL 或 so，避免使用 `build/bin` 中可能存在的旧链接。
- Windows 下只从 `build/bin` 额外复制 allowlist 中的本地 DLL，目前是 `quickui.dll`。
- 按 `tools/dependencies.yaml` 复制第三方运行库，默认只处理 release 条目，跳过 `config: debug`。
- Windows 下复制 MSVC runtime、Windows SDK 的 `dxcompiler.dll` 和 `dxil.dll`，并运行 `windeployqt` 部署 Qt 运行时。
- Windows 下优先使用 `cmake/ConfigQT.cmake` 或 `CMakeCache.txt` 中的 Qt6 路径，最后才回退到 PATH，避免误用旧 Qt。
- Windows release 包会删除 `qmltooling/qmldbg_*.dll` 调试插件。
- Linux/macOS 下复制 Qt QML/plugins/translations，扫描 ELF 依赖，并在可用时用 `patchelf` 设置 rpath。
- 写入 `.dltool_package` marker，供下次安全清理输出目录使用。

常用命令：

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\package_app.py
```

```bash
python tools/package_app.py
```

指定构建目录和输出目录：

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\package_app.py --build-dir build --install-dir install
```

跳过 Qt 部署和系统运行库，适合快速检查脚本复制逻辑：

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\package_app.py --install-dir build\package_check --skip-windeployqt --skip-system-libs
```

常用参数：

| 参数 | 说明 |
|------|------|
| `--build-dir` / `-BuildDir` | CMake 构建目录，默认 `build`。 |
| `--install-dir` / `-InstallDir` | 发布包输出目录，默认 `install`。 |
| `--dependencies` | 依赖清单路径，默认 `tools/dependencies.yaml`。 |
| `--windeployqt` / `-WinDeployQt` | 显式指定 `windeployqt.exe`。 |
| `--qt-root` / `-QtRoot` | Linux/macOS 下显式指定 Qt 安装根目录。 |
| `--skip-windeployqt` / `--skip-qt` | 跳过 Qt 部署。 |
| `--skip-dependencies` | 跳过 `dependencies.yaml` 中声明的第三方依赖。 |
| `--skip-system-libs` | 跳过 MSVC/SDK 或 ELF 系统依赖收集。 |
| `--include-pdb` | 复制 PDB 调试符号。 |
| `--include-qml-module-dir` | 额外复制散装 QML 模块目录。 |
| `--no-clean` | 不清理输出目录，直接复用。 |
| `--force-clean` | 即使输出目录没有 `.dltool_package` marker，也强制清理。 |

默认清理输出目录时，如果目录非空且没有 `.dltool_package` marker，脚本会拒绝删除。这样可以避免把普通目录误当发布包目录清掉。

## `link_dependencies.py`

`link_dependencies.py` 用于开发阶段准备 build 目录运行环境。它会把项目 DLL 和第三方运行库链接到 `build/bin`，方便直接运行 `build/bin/dltool.exe` 或测试程序。

它会执行这些步骤：

- 将 `build/dltool` 链接到 `build/bin/dltool`。
- 将 `build/dltool/<module>/dltool_*.dll` 链接到 `build/bin`。
- 按 `tools/dependencies.yaml` 把第三方运行库链接到配置的 `destinations`。
- 默认使用 release 配置，跳过 `config: debug` 的依赖和成对 debug DLL。
- Windows 下优先创建符号链接，失败后对文件回退为 hardlink、对目录回退为 junction。

常用命令：

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\link_dependencies.py
```

只链接项目输出，不链接第三方依赖：

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\link_dependencies.py --skip-external
```

显式链接 debug 依赖：

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\link_dependencies.py --config debug
```

## `dependencies.yaml`

`dependencies.yaml` 是 `package_app.py` 和 `link_dependencies.py` 共用的运行时依赖清单。

基本格式：

```yaml
dependencies:
  - name: sqlite
    cmake: cmake/ConfigSQLite.cmake
    root: SQLITE_ROOT
    destinations:
      - build/bin
    windows:
      - sqlite3.dll
```

字段说明：

| 字段 | 说明 |
|------|------|
| `name` | 依赖名称，只用于日志和排查。 |
| `config` | 可选。`release` 只在 release 模式处理，`debug` 只在 debug 模式处理；不写表示通用。 |
| `cmake` | 可选。用于从 CMake 配置文件读取 `root` 指向的变量。 |
| `root` | 依赖根目录，或者 CMake cache/config 变量名。 |
| `destinations` | `link_dependencies.py` 的链接目标目录。打包时 Windows 复制到包根目录，Linux/macOS 复制到 `lib/`。 |
| `windows` / `linux` / `macos` | 当前平台要复制或链接的文件模式，支持 `*` 和 `?`。 |
| `all` | 所有平台通用的文件模式。 |

`root` 解析顺序：

1. 如果是绝对路径、相对路径或带路径分隔符的值，直接按路径解析。
2. 否则先从 `build/CMakeCache.txt` 查同名变量。
3. 再从 `cmake` 指定的 CMake 文件里读取同名 `set(...)`。

DLL 过滤规则：只有当 `xxx.dll` 和 `xxxd.dll` 成对存在时，脚本才把 `xxxd.dll` 视为 debug 变体。这样不会误删文件名本身以 `d` 结尾的 release DLL。
