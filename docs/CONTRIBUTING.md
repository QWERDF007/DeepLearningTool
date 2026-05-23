# 贡献指南

本文档说明 DeepLearningTool 的本地开发、测试、提交流程和评审检查点。

## 分支模型

- `main`：稳定分支，用于发布。
- `dev`：日常开发集成分支。
- `feature/<name>`：新特性。
- `fix/<name>`：缺陷修复。
- `docs/<name>`：文档更新。

## 提交信息

格式：

```text
type(scope): summary
```

常用类型：`feat`、`fix`、`refactor`、`docs`、`test`、`build`、`chore`。

示例：

```text
feat(data): add category statistics model
docs(api): document database module
```

## 本地构建

Windows 建议使用 Ninja：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

Linux / macOS：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

## 本地打包

Windows：

```powershell
tools\package_app.bat
```

Linux：

```bash
bash tools/package_app.sh
```

Windows / Linux 打包脚本默认从 `build/` 读取构建产物并输出到 `install/`。当前项目的 QML 模块已嵌入 Qt 资源，默认不复制散装的 `build/dltool` 模块目录，只复制可执行程序、项目动态库和运行时依赖。若需要调试散装 QML 模块，可在 Windows 使用 `-IncludeQmlModuleDir`，在 Linux 使用 `--include-qml-module-dir`。

Linux 下还会复制 Qt QML/插件目录，并通过 `ldd` 收集 ELF 依赖。若 Qt 不在系统可发现路径中，可显式传入：

```bash
bash tools/package_app.sh --qt-root /opt/Qt/6.8.0/gcc_64
```

注意：

- 当前 `cmake/ConfigQT.cmake` 写有本机 Qt 安装路径，例如 `Qt6_ROOT`。换机或 CI 构建时需要改为目标环境路径，或通过 CMake cache/toolchain 传入。
- 根构建脚本启用 CUDA 语言；如果本机没有 CUDA 工具链，需要先确认当前构建环境是否支持。
- 当前 C++ 标准为 C++17。

## 运行测试

当前 `tests/CMakeLists.txt` 只启用 UI 测试：

```powershell
cmake --build build --target tst_dltool_ui
ctest --test-dir build -V
```

测试源码位于：

```text
tests/ui/
├── main.cpp
├── test_Utils.cpp
├── tst_main.qml
└── *Test.qml
```

## 开发流程

1. 从最新 `dev` 创建分支。
2. 修改代码和文档，保持模块边界不被破坏。
3. 本地构建并运行相关测试。
4. 发起 PR 到 `dev`。
5. PR 描述中说明变更原因、范围、风险点和测试结果。

## PR 检查清单

- 架构：没有新增低层模块对高层模块的反向依赖。
- CMake：新增库模块使用 `add_plugin_library()`，不需要 QML 时显式 `NO_QML_MODULE`。
- Include：跨模块包含使用模块名前缀，不使用跨模块相对路径。
- 数据库：DDL 和 sqlpp11 表定义放在 `src/database/include/database/ddl/`；UI/Project 不直接操作 SQLite 连接。
- 数据模型：`QAbstractItemModel` role 名称已同步到文档或 QML 使用处。
- QML：复杂业务逻辑不堆在 QML 中，通用控件放在 `src/ui/controls/`。
- 资源：字体、图标和静态资源统一通过 `assets/assets.qrc`。
- 测试：影响 UI 控件、数据模型或项目生命周期时补充或更新测试。
- 文档：接口、目录、构建方式或模块边界变化时同步 `docs/`。

## 第三方依赖

- 现有依赖：`sqlpp11`、`spdlog`、`nlohmann/json`。
- 新增依赖应放在 `3rdparty/` 或通过统一 CMake 配置管理。
- PR 中说明许可证、平台兼容性、体积和维护影响。

## 缺陷报告

报告问题时建议提供：

- 复现步骤。
- 系统、编译器、Qt 版本和构建类型。
- 相关日志、截图或最小复现项目文件。
- 预期行为和实际行为。
