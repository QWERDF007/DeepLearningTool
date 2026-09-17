# DeepLearningTool 架构

DeepLearningTool 是一个由 Qt 6/QML 驱动界面、C++ 驱动业务、SQLite 驱动项目数据、Python 驱动模型任务的桌面应用。模块边界和依赖以 [`src/CMakeLists.txt`](../src/CMakeLists.txt) 及各模块 CMake 文件为准；本页只说明运行时关系。

![DeepLearningTool 架构总览](assets/ARCHITECTURE_DIAGRAM.svg)

> 架构图是当前源码和 CMake 关系的静态总览；模块依赖、公开类型和任务契约发生变化时，应同步更新图和本页说明，最终以源码和构建配置为准。

## 分层关系

源码构建顺序为：

```text
common -> core -> database -> ui -> parameter -> settings
       -> data -> model -> feature -> project -> tool
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
                 ├─ ProjectDataBase（持久化门面，协调各 Repository）
                 ├─ DataManager（数据工作区门面，协调领域用例服务）
                 ├─ FeatureManager（高级功能聚合）
                 │    ├─ ImageSearchController
                 │    ├─ RoiSearchController
                 │    ├─ RegionSearchController（DINO 区域检索）
                 │    ├─ ImageClusterController
                 │    ├─ RoiClusterController
                 │    ├─ SmartAnnotationController
                 │    └─ FewShotLearningController
                 ├─ ModelManager
                 ├─ ModelTaskController
                 ├─ ModelTestTaskManager
                 └─ TaskManager
```

`Project` 是项目级聚合对象，不把数据库细节暴露给 QML。`ProjectManager` 负责创建、打开、关闭和删除项目；`Project` 在自己的生命周期内创建和释放数据、模型、功能和任务对象。实现入口见 [`src/project/include/project/Projects.h`](../src/project/include/project/Projects.h) 和 [`src/tool/main.cpp`](../src/tool/main.cpp)。

## 数据工作区链路

```text
QML 页面
  -> DataManager（门面）
      -> Qt Model / GlobalFilter / Statistics
      -> 领域用例服务（DataImportService / DataExportService / ImageTransferService / DatasetSplitService / ClusterWritebackService）
      -> ProjectDataBase
          -> 仓储层（ProjectRepository / DatasetRepository / ImageRepository / LabelRepository / ModelRepository / TagRepository）
```

`data` 负责数据集、图片、类别、标注、标签、过滤、统计及格式转换；数据持久化统一委托至 `database` 仓储层。导入导出格式的扩展点和页面入口在 [`src/data/README.md`](../src/data/README.md)，表定义在 [`src/database/include/database/ddl/`](../src/database/include/database/ddl/)。

## 特征检索与高级功能链路

```text
QML 页面 / 对话框
  -> FeatureManager（聚合入口）
      ├─ RegionSearchController: DINO 多尺度细粒度区域检索
      │    -> 校验/构建/加载 region_search/ 强契约索引
      │    -> 调用 InferRT DinoRegionSearch 管线 (Fusion -> FineMatch -> FineExtract -> Output)
      │    -> 结果候选去重并作为标注实例回写 DataManager
      ├─ ImageSearchController / RoiSearchController: FAISS 向量检索
      │    -> 将搜索结果写回 DataManager.GlobalFilter 应用视图过滤
      ├─ ImageClusterController / RoiClusterController: HDBSCAN 特征聚类
      │    -> 委托 ClusterWritebackService 将聚类分配写回数据集
      ├─ SmartAnnotationController: SAM 交互式智能标注与 B 样条亚像素轮廓后处理
      └─ FewShotLearningController: FS-SAM2 训练/推理任务链编排
```

各控制器的生命周期、InferRT 契约与配置加载约定见 [`src/feature/README.md`](../src/feature/README.md)。

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

QML 模块通过 Qt 的 QML 类型注册暴露对象。应用级入口包括 `ProjectManager`、`GlobalSettings` 以及 UI 服务；`TaskManager` 与数据、模型 manager 由项目对象持有。页面负责布局、绑定和轻量交互，协议、持久化、任务状态和评估计算留在 C++。

顶层页面由 [`src/tool/qml/Content.qml`](../src/tool/qml/Content.qml) 装配，领域页面分别位于 `src/project/qml/`、`src/data/qml/` 和 `src/model/qml/`。模块 URI、公开类型和目录入口见 [模块索引](MODULES.md)。

## 异步与线程边界

- GUI 线程创建和操作 QObject、Qt Model 及 QML 状态。
- 数据导出、文件复制、任务准备、外部进程和评估计算在后台执行。
- 后台只接收路径、参数快照和其它纯值输入；结果通过 Qt 信号或 queued connection 回到 GUI 线程。
- 取消必须沿任务控制器和取消令牌传递，完成、失败和停止后的迟到事件不能重新打开终态任务。
- 需要跨线程更新 UI 服务时，复用现有服务 API 或 `Qt::QueuedConnection`。

项目关闭顺序由 [`Project::shutdown()`](../src/project/Projects.cpp) 统一组织，各领域控制器负责自己的执行者与外部进程。关闭入口、完成回调和共享线程池的收敛要求及待补边界见 [架构改进方案](../final_plan.md)，具体步骤与验收见 [实施规格](REFACTOR_SPEC.md)；统一关闭入口的存在不代表所有关闭场景已验证。

这些规则的具体实现位于 `src/model/ModelTaskController.*`、`src/model/ModelTaskPreparation.*`、`src/model/ExternalModelTaskRunner.*` 和对应评估引擎文件。
