# 开发指南

## 环境

当前工程由根 [`CMakeLists.txt`](../CMakeLists.txt) 启用 C、C++ 和 CUDA。C++ 标准在 [`cmake/ConfigCompiler.cmake`](../cmake/ConfigCompiler.cmake) 中设为 C++20。Qt、SQLite、OpenCV、InferRT 和 CUDA 的查找及本机路径配置位于 [`cmake/`](../cmake/)，换机器时先检查这些配置，不要把个人绝对路径当成通用默认值。

Qt 配置需要包含 Core、Gui、Quick、QML、Widgets、Charts、Network、WebEngineQuick 和 QuickTest 等当前 CMake 所需组件。第三方代码和依赖入口位于 [`3rdparty/`](../3rdparty/) 与 [`tools/dependencies.yaml`](../tools/dependencies.yaml)。

## 获取依赖

如果仓库使用的 Git 子模块尚未初始化：

```powershell
git submodule update --init --recursive
```

根 CMake 会检查子模块状态。配置失败时先查看 CMake 输出中的缺少组件和 `cmake/Config*.cmake` 路径。

## 配置和构建

```powershell
cmake -S . -B build -DDLT_BUILD_TESTS=ON
cmake --build build --config Release --parallel 4
```

常用构建选项：

| 选项 | 默认值 | 作用 |
| --- | --- | --- |
| `DLT_BUILD_TESTS` | `ON` | 构建并注册测试 |
| `DLT_BUILD_DOCS` | `OFF` | 文档构建开关；当前 Markdown 文档直接维护 |
| `DLT_ENABLE_SANITIZER` | `OFF` | 在支持的编译器上启用 sanitizer |

构建产物默认位于 `build/bin/`、`build/lib/` 和 `build/dltool/` 下。Windows 开发运行时如需准备 DLL/QML 运行环境，可使用 [`tools/link_dependencies.py`](../tools/link_dependencies.py)；发布包使用 [`tools/package_app.py`](../tools/package_app.py)。

## 启动程序

应用入口是 `build/bin/dltool.exe`（Windows）或对应平台的 `dltool`。直接运行前应先完成依赖链接，并确认 Qt QML import path 和第三方动态库可用。测试使用的离屏软件渲染环境不等同于真实桌面运行环境。

## 运行任务所需的 Python

模型任务通过全局设置取得 Python 环境目录，再由 `ExternalModelTaskRunner` 启动外部脚本。项目级测试可以在启动测试时覆盖环境目录：

```powershell
python tools\run_project_tests.py `
    --project-layer set-python-env `
    --python-env 'D:\Software\anaconda3\envs\py312'
```

Python 任务协议、参数和任务目录由 `src/model/` 与 `3rdparty/EasyTrain/` 的实现共同定义；修改一侧时要同时检查另一侧和项目级测试。

## 修改路径

1. 先定位所属模块和现有扩展点，阅读模块 README、头文件和 CMake。
2. 跨模块数据通过 manager、明确的纯值结构或 provider 接口传递，不绕过数据库访问边界。
3. 长任务使用现有任务控制器、进度和日志链路，耗时操作放入后台。
4. 参数和设置优先修改 `config/models/` 或 `config/settings/`，再检查生成模型和持久化读写。
5. 行为变更补充相邻的 C++、QML 或项目级测试，并通过 CTest 执行。
6. 文档只记录稳定规则和索引；接口变化以源码为准，避免在文档中复制完整 API。

## 日志和故障定位

- 应用日志通过模块 `Logger` 注册到 spdlog，QML 日志经 `UILogger` 展示。
- 任务日志由模型存储服务放在训练目录或测试任务目录。
- Python 任务的 stdout/stderr 由 `ExternalModelTaskRunner` 写入任务日志。
- 测试失败时优先保留 CTest 的 `--output-on-failure` 输出、任务日志和生成目录，不要直接运行测试可执行文件绕过 CTest 的运行环境设置。

## 相关入口

- [架构](ARCHITECTURE.md)
- [模块索引](MODULES.md)
- [数据模型与存储](DATA_MODEL.md)
- [测试指南](TESTING.md)
- [工具脚本说明](../tools/README.md)
