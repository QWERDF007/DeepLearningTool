# DeepLearningTool 架构

DeepLearningTool 是一个由 Qt 6/QML 驱动界面、C++ 驱动业务、SQLite 驱动项目数据、Python 驱动模型任务的桌面应用。模块边界和依赖以 [`src/CMakeLists.txt`](../src/CMakeLists.txt) 及各模块 CMake 文件为准；本页只说明运行时关系。

![DeepLearningTool 架构总览](assets/ARCHITECTURE_DIAGRAM.svg)

> 架构图是当前源码和 CMake 关系的静态总览；模块依赖、公开类型和任务契约发生变化时，应同步更新图和本页说明，最终以源码和构建配置为准。

## 分层关系

源码构建顺序为：

```text
common -> core -> database -> ui -> parameter -> settings
       -> model -> feature -> data -> project -> tool
```

这表示构建和依赖的主方向，不表示所有模块之间都是线性依赖。边界如下：

| 层 | 责任 | 代表入口 |
| --- | --- | --- |
| 基础设施 | 日志、崩溃处理、路径和通用工具 | `src/common/` |
| 核心定义 | 任务类型等跨领域定义 | `src/core/` |
| 持久化 | SQLite 连接、DDL 和数据库访问对象 | `src/database/` |
| 参数与设置 | 参数元数据、动态选项、全局设置 | `src/parameter/`、`src/settings/` |
| UI 基础 | QML 公共控件、日志、进度和图表适配 | `src/ui/` |
| 领域业务 | 数据、模型和高级功能 | `src/data/`、`src/model/`、`src/feature/` |
| 项目聚合 | 项目生命周期以及项目内对象所有权 | `src/project/` |
| 应用装配 | Qt 应用、QML 引擎和顶层导航 | `src/tool/` |

低层不反向依赖高层。数据库访问集中在 `database`；QML 通过项目对象、manager 和 Qt Model 访问业务数据；耗时 I/O、数据导出和外部任务不应在 QML 线程执行。

## 运行时对象关系

```text
dltool/main.cpp
  ├─ 初始化 CrashHandler、spdlog、QApplication、QQmlApplicationEngine
  └─ 加载 dltool.tool/Main.qml
       └─ ProjectManager（QML 单例）
            └─ currentProject: Project
                 ├─ ProjectDataBase
                 ├─ DataManager
                 ├─ FeatureManager
                 ├─ ModelManager
                 ├─ ModelTaskController
                 ├─ ModelTestTaskManager
                 └─ TaskManager
```

`Project` 是项目级聚合对象，不把数据库细节暴露给 QML。`ProjectManager` 负责创建、打开、关闭和删除项目；`Project` 在自己的生命周期内创建和释放数据、模型、功能和任务对象。实现入口见 [`src/project/include/project/Projects.h`](../src/project/include/project/Projects.h) 和 [`src/tool/main.cpp`](../src/tool/main.cpp)。

## 数据工作区链路

```text
QML 页面
  -> DataManager
      -> Qt Model / GlobalFilter / Statistics
      -> ProjectDataBase
      -> DataImporter / DataExporter
```

`data` 负责数据集、图片、类别、标注、标签、过滤、统计及格式转换；`database` 只负责存取。导入导出格式的扩展点和页面入口在 [`src/data/README.md`](../src/data/README.md)，表定义在 [`src/database/include/database/ddl/`](../src/database/include/database/ddl/)。

## 模型任务链路

```text
模型页或任务中心
  -> ModelTaskController / TaskManager
  -> 后台准备：数据导出、文件列表、数据库和目录
  -> ExternalModelTaskRunner
  -> EasyTrain / Python 外部进程
  -> TCP 任务事件
  -> TaskManager 与 ModelManager 更新状态和结果
```

任务状态由 `TaskManager` 管理。控制器负责准备和编排，外部运行器只负责 Python 进程，任务中心不直接启动 Python。训练和测试输入由纯值 `ModelTaskRequest` 传入后台，避免将 `QObject` 或数据库对象带入工作线程。详细契约见 [`src/model/README.md`](../src/model/README.md)。

测试任务完成后，评估链路为：

```text
test.txt + task.db + project .dlpro + pred/*.tiff
  -> IEvaluationEngine 子类
  -> EvaluationResult
  -> ModelEvaluationViewModel 子类
  -> QML 评估面板、图表、混淆矩阵和实例列表
```

评估引擎在后台读取文件并构造结果，ViewModel 负责 Qt Model、过滤、选择和展示。推理参数与评估参数的语义边界以 [`GRILL_ME_EVALUATION_PARAMETER_SPLIT.md`](GRILL_ME_EVALUATION_PARAMETER_SPLIT.md) 为准，异常检测可视化边界以 [`GRILL_ME_ANOMALY_SEGMENTATION_HEATMAP.md`](GRILL_ME_ANOMALY_SEGMENTATION_HEATMAP.md) 为准。

## QML 边界

QML 模块通过 Qt 的 QML 类型注册暴露对象。应用级单例包括 `ProjectManager`、`GlobalSettings`、`TaskManager` 以及 UI 服务；项目对象拥有数据和模型 manager。页面负责布局、绑定和轻量交互，协议、持久化、任务状态和评估计算留在 C++。

顶层页面由 [`src/tool/qml/Content.qml`](../src/tool/qml/Content.qml) 装配，领域页面分别位于 `src/project/qml/`、`src/data/qml/` 和 `src/model/qml/`。模块 URI、公开类型和目录入口见 [模块索引](MODULES.md)。

## 异步与线程边界

- GUI 线程创建和操作 QObject、Qt Model 及 QML 状态。
- 数据导出、文件复制、任务准备、外部进程和评估计算在后台执行。
- 后台只接收路径、参数快照和其它纯值输入；结果通过 Qt 信号或 queued connection 回到 GUI 线程。
- 取消必须沿任务控制器和取消令牌传递，完成、失败和停止后的迟到事件不能重新打开终态任务。
- 需要跨线程更新 UI 服务时，复用现有服务 API 或 `Qt::QueuedConnection`。

这些规则的具体实现位于 `src/model/ModelTaskController.*`、`src/model/ModelTaskPreparation.*`、`src/model/ExternalModelTaskRunner.*` 和对应评估引擎文件。
