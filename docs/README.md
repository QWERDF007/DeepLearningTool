# DeepLearningTool 文档索引

DeepLearningTool 是一个基于 C++17、Qt 6/QML、CMake 和 SQLite 的深度学习数据标注桌面工具。当前项目主要覆盖项目管理、数据集管理、图像导入、目标检测标注、语义分割多边形标注、图像标签、多类型过滤（支持反向过滤）、图像相似度搜索（基于 InferRT + FAISS）、智能标注、模型记录管理、训练/测试参数配置和统计等工作流。

## 文档列表

| 文档 | 说明 |
|------|------|
| [ARCHITECTURE.md](./ARCHITECTURE.md) | 系统架构总览、模块边界和依赖关系 |
| [CODE_STRUCTURE.md](./CODE_STRUCTURE.md) | 当前目录结构、模块职责、构建目标和数据流 |
| [API_REFERENCE.md](./API_REFERENCE.md) | C++/QML 公开接口、模型 role 和常用单例 |
| [CODING_STYLE.md](./CODING_STYLE.md) | C++、QML、CMake、数据层和资源约定 |
| [CONTRIBUTING.md](./CONTRIBUTING.md) | 本地开发、测试、PR 与评审流程 |

## 快速导航

### 了解项目

1. 阅读 [ARCHITECTURE.md](./ARCHITECTURE.md) 了解分层和模块依赖。
2. 阅读 [CODE_STRUCTURE.md](./CODE_STRUCTURE.md) 了解源码目录、QML 模块和测试结构。

### 开发参考

1. 阅读 [API_REFERENCE.md](./API_REFERENCE.md) 查找 QML 单例、模型 role 和主要 C++ 接口。
2. 阅读 [CODING_STYLE.md](./CODING_STYLE.md) 对齐命名、模块边界、CMake 和数据层约定。

### 参与贡献

1. 阅读 [CONTRIBUTING.md](./CONTRIBUTING.md) 了解构建、测试和提交检查清单。

## 当前模块

| 模块 | 位置 | 目标 | QML URI | 职责 |
|------|------|------|---------|------|
| Common | `src/common/` | `dltool_common` | 无 | 日志、崩溃处理、工具函数、单例模板 |
| Core | `src/core/` | `dltool_core` | `dltool.core` | 深度学习任务类型等跨模块核心定义 |
| Database | `src/database/` | `dltool_database` | 无 | SQLite 连接池、项目库、最近项目库、设置库、DDL |
| Settings | `src/settings/` | `dltool_settings` | `dltool.settings` | 全局/项目/数据/高级/UI 设置与持久化 |
| UI | `src/ui/` | `dltool_ui` | `dltool.ui` | 主题、字体、图标、日志/进度单例、自定义 QML 控件 |
| Model | `src/model/` | `dltool_model` | `dltool.model` | 模型记录、模型结构注册、训练/测试参数和页面骨架 |
| Feature | `src/feature/` | `dltool_feature` | `dltool.feature` | 图像相似搜索、智能标注等模型推理/特征计算能力 |
| Data | `src/data/` | `dltool_data` | `dltool.data` | 数据集、图像、标注、标签、过滤、统计模型和数据导入导出 |
| Project | `src/project/` | `dltool_project` | `dltool.project` | 项目生命周期、最近项目、项目级对象聚合 |
| Tool | `src/tool/` | `dltool` | `dltool.tool` | 应用入口、顶层 QML、主窗口布局 |

每个 `src/` 一级模块目录下还有模块级 `README.md`，用于说明该模块的架构设计、职责边界和扩展约定。

## 常用命令

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
ctest --test-dir build -V
```

打包当前构建产物：

```powershell
tools\package_app.bat
```

```bash
bash tools/package_app.sh
```

`cmake/ConfigQT.cmake` 当前写有本机 Qt 路径，迁移环境时需要先调整 `Qt6_ROOT` 或改为外部传入。

## 相关资源

- 根目录 [DESIGN.md](../DESIGN.md) - 产品设计文档
- 根目录 [README.md](../README.md) - 项目简介
