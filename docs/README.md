# DeepLearningTool 文档

这是仓库级文档入口。文档只保留跨模块、跨命令的稳定约定；具体接口、文件清单和参数定义以源码、CMake 与 YAML 为准。

## 按目的阅读

| 目的 | 文档 |
| --- | --- |
| 理解模块边界、对象关系和任务链路 | [架构](ARCHITECTURE.md) |
| 查找模块入口和模块级说明 | [模块索引](MODULES.md) |
| 理解项目、模型和任务文件如何落盘 | [数据模型与存储](DATA_MODEL.md) |
| 配置环境、构建程序和开发构建 | [开发指南](DEVELOPMENT.md) |
| 运行普通测试和项目级分层测试 | [测试指南](TESTING.md) |
| 查看已确认的专项设计决策 | [访谈记录](#设计记录) |

## 单一事实源

- 构建入口和选项：[`CMakeLists.txt`](../CMakeLists.txt) 与 [`cmake/`](../cmake/)。
- 模块目标和依赖：[`src/CMakeLists.txt`](../src/CMakeLists.txt) 及各模块的 `CMakeLists.txt`。
- 模型和设置参数：[`config/models/`](../config/models/) 与 [`config/settings/`](../config/settings/)。
- 数据库表结构：[`src/database/include/database/ddl/`](../src/database/include/database/ddl/)。
- 测试注册和执行实现：[`tests/`](../tests/)、[`tools/run_model_tests.py`](../tools/run_model_tests.py)、[`tools/run_project_tests.py`](../tools/run_project_tests.py)。

文档中的接口名称只用于定位源码，不替代头文件注释或实现。发生代码、CMake 或配置变更时，应优先更新对应事实源，再检查本目录中的导航和稳定约定。

## 设计记录

以下文件是已确认的专项访谈记录，保留原文作为设计决策依据：

- [模型模块重构访谈](GRILL_ME_MODEL_REFACTOR.md)
- [推理与评估参数拆分访谈](GRILL_ME_EVALUATION_PARAMETER_SPLIT.md)
- [异常检测实例分割与热力图访谈](GRILL_ME_ANOMALY_SEGMENTATION_HEATMAP.md)
- [动态自适应最优阈值搜索访谈](GRILL_ME_ADAPTIVE_THRESHOLD_SEARCH.md)
