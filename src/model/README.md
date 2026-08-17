# model 模块说明

## 模块职责

`model` 负责模型记录、模型参数、模型数据集选择，以及模型任务的完整生命周期。

模型任务只有一条执行链：

```text
模型页 / 任务中心
  -> ModelTaskController 创建或复用任务记录
  -> TaskManager 标记 Preparing 并通知控制器
  -> DataManager 后台导出数据集
  -> prepareModelTask() 后台导出文件列表、更新数据库并创建进程规格
  -> ExternalModelTaskRunner 启动 Python
  -> Python TCP 事件更新 TaskManager 和模型结果
```

任务中心不启动 Python；它只保存任务状态、显示任务表并接收/发送任务通信消息。

## 核心对象

### TaskManager

`TaskManager` 是应用级 QML 单例，同时是任务中心的 `QAbstractTableModel`。

- 保存唯一的 `Task` 记录，不维护任务快照、`QVariantMap` 副本或额外事件路由器。
- 负责任务表的局部插入、删除和状态/进度更新。
- 管理 `TaskCommunicationServer`，直接解析 Python 的 TCP 事件。
- `startTask()` 将任务置为 `Preparing` 并发出 `taskStartRequested(task_id)`。
- `stopTask()` 将任务置为 `Stopping`、发送 TCP 停止命令并发出 `taskStopRequested(task_id)`。

任务状态如下：

```text
Pending -> Preparing -> Running -> Stopping -> Stopped
                         |
                         +-> Finished / Failed
```

`Preparing` 表示数据集导出、目录创建和配置写入正在后台执行；Python 进程实际发出
`started` 信号后才进入 `Running`。停止发生在 `Preparing` 时，任务会立即收敛为
`Stopped`，已在后台运行的准备回调会被忽略，不会再启动 Python。

### ModelTaskController

`ModelTaskController` 是项目级的任务编排入口，由 `project::Project` 创建并通过
`Project.currentProject.modelTaskController` 暴露给 QML。

它直接串联完整流程：

1. `startModelTask()` 校验模型并创建或复用任务记录。
2. 调用 `TaskManager::startTask()`；模型页面与任务中心从这里汇合。
3. 收到 `taskStartRequested` 后，在 GUI 线程提取轻量任务输入。
4. 调用 `DataManager::runDatasetExportAsync()` 在后台导出选中数据集；不需要导出数据集的任务直接使用 `DataOperationWorkflow::start()`。
5. 后台调用 `prepareModelTask()` 导出文件列表、更新 `model.db`/`task.db` 并生成
   `ExternalProcessSpec`。
6. `ExternalModelTaskRunner` 启动 Python；进程实际启动后控制器将任务置为 `Running`。
7. Python 事件到达后，控制器把训练/测试状态、进度、指标和消息写回模型 `extra_data`，QML 模型列表自动刷新。
8. 进程退出时，控制器按退出码将任务收敛为 `Stopped`、`Finished` 或 `Failed`。

控制器持有项目上下文：`ModelManager`、`DataManager`、`TaskManager` 和一个
`ExternalModelTaskRunner`。它不持有数据库对象，也不把 `DataManager` 传入后台准备函数。

### ModelTaskRequest 与 prepareModelTask

`ModelTaskRequest` 是 GUI 线程传入后台的唯一纯值输入，包含：

- 任务 ID 和任务类型；
- 框架定义与任务通信端点；
- 当前模型名称、架构和参数；
- 当前数据集选择。

它不包含 `QObject`、`IModel`、`DataManager` 或数据库对象。后台函数
`prepareModelTask()` 负责：

- 创建 `models/<模型名>/train/` 或 `models/<模型名>/test/<任务名>/` 下的目录；
- 通过 `ModelDatasetOrganizer` 导出训练/验证图像列表 `train/train.txt` 与 `train/validation.txt`（随数据集和类别选择变化），以及测试任务专属
  的 `test.txt`；
- 通过数据库对象写入模型参数、测试参数和数据集/类别选择；
- 读取全局 Python 环境路径并生成 `ExternalProcessSpec`。

模型目录始终按模型名组织，测试任务名称经过校验后作为目录名，UUID 只作为稳定身份：

```text
models/<模型名>/
  model.db
  datasets/
    masks/
  train/
    train.txt
    validation.txt
    weights/
    logs/
  test/
    logs/<测试任务 UUID>.log
    <测试任务名>/
      task.db
      test.txt
      pred/
```

训练/验证 `train.txt`/`validation.txt` 位于 `train/` 下，每行只含 `image_id,image_path`。Python 从列表拿到 `image_id` 后到 `datasets/masks` 查找 `<image_id>.png`：存在 mask 即异常图（mask 像素 0 为背景、255 为异常区域），不存在则为正常图，因此不再生成 labels JSON/YAML。测试文件列表位于测试任务自己的目录，避免多个任务共享或覆盖文件。测试任务不生成
测试标签旁路文件；用户点击开始测试时，后台重新导出任务目录下的 `test.txt`，清理该任务的 `pred/`
并重新调用 Python 推理。Python 只写入模型预测产物（例如异常分数 TIFF 和任务数据库中的
预测记录）。评估页面按需读取任务 `test.txt`、任务数据库和项目数据库，由 C++ 在后台
从内存中的图像、类别和标注构造 GT 并评估；指标、图表、图像记录和实例事件只保存在
当前进程的 `ModelEvaluationViewModel` 中，不生成评估报告文件。

评估页面只在选中测试任务时惰性触发。相同任务的 ViewModel 在内存中复用，切换任务不会
重复评估；修改评估参数会清空当前结果并重新读取 `test.txt`、数据库和预测产物评估，
修改推理参数或数据集选择则等待下一次用户主动开始测试。重新打开项目时没有旧的内存结果，
选中任务后重新读取相同输入并后台评估，打开行为不弹窗；只有用户点击开始测试触发的评估
完成或失败才通知。

### 数据导出

数据导出属于 `data` 模块：

- `ModelTaskController` 只调用 `DataManager::runDatasetExportAsync()`。
- `data` 在工作线程创建 `DatasetExportSource`，其实现直接读取 `DataManager` 的内存数据。
- `ModelDatasetOrganizer` 只接收 `DatasetExportSource`，不依赖 `DataManager` 或数据库。

因此大数据集导出、文件复制和配置写入均不阻塞 GUI 线程。

### ExternalModelTaskRunner

`ExternalModelTaskRunner` 只负责外部 Python 进程：

- 按 `ExternalProcessSpec` 设置解释器、参数、工作目录和环境变量；
- 将 stdout/stderr 写入模型日志；
- 发出 `taskStarted`、`taskStartFailed` 和 `taskFinished`；
- 处理 `terminate()`，超时后 `kill()`。

它不修改任务表，也不读取模型、数据集或数据库。

## UI 入口

模型页面调用：

```qml
ProjectManager.currentProject.modelTaskController.startModelTask(modelUuid, taskType)
```

任务中心对已有任务调用：

```qml
TaskManager.startTask(taskId)
```

前者负责确保任务记录存在；后者直接重启已有可启动记录。两者进入 `Preparing` 后都由
同一个 `ModelTaskController` 完成后续后台准备和 Python 启动。

## Python 任务通信

Python 脚本通过启动参数获得本地 TCP 地址和任务 ID：

```text
--dltool_task_host <host>
--dltool_task_port <port>
--dltool_task_id <task_id>
```

脚本发送 `running`、`paused`、`stopped`、`finished`、`failed`、`error`、进度和 ETA。
`TaskManager` 先更新任务表，再发出 `taskMessageReceived`；`ModelTaskController` 随后刷新该模型的
训练或测试 `extra_data`。已停止、已完成或已失败的任务不会被迟到事件重新打开。

停止时，`TaskManager` 同时向 TCP 客户端发送 `stop` 命令，并通知控制器终止对应 Python 进程。

## 小样本学习

FS-SAM2 属于小样本学习专用流程，不接入普通模型测试任务的评估适配器；普通模型测试
只维护一个测试数据集选择，并通过测试任务管理器切换数据库、文件列表、预测产物和内存
评估结果。

## 测试评估展示

评估按方法拆分为引擎层级：`EvaluationEngineRegistry` 首次访问时自动注册
`AnomalyEvaluationEngine`、`DetectionEvaluationEngine`、`SegmentationEvaluationEngine`；
`IEvaluationEngine` 基类实现加载图像/预测、源文件过滤、取消检查与强类型
`EvaluationResult` 组装的公共骨架，子类实现类别构建、实例/图像计数、实例事件、
方法图表与混淆矩阵。后台通过 `std::shared_ptr<EvaluationResult>` 跨线程回 GUI，
`ModelEvaluationViewModel`（抽象基类）及其方法子类负责 Qt Model、过滤、选择和
过滤后的后台重聚合。`EvaluationPanelRegistry.qml` 集中维护方法到子面板组件的映射，
`TestEvaluationPanel.qml` 只负责 SplitView 布局与状态遮罩，通过 Loader 装配面板，
不在 QML 中遍历或计算评估事件。

## 扩展约定

新增框架或任务类型时：

1. 在 `ModelRegistry` 注册框架、架构、脚本和数据集导出规则。
2. 在 `ModelTaskTypes` 定义任务描述与配置文件名。
3. 在 `ModelDatasetOrganizer` 中实现框架数据集导出规则。
4. 评估方法通过 `EvaluationEngineRegistry` 与 `EvaluationViewModelRegistry` 注册
   对应子类；QML 面板在 `EvaluationPanelRegistry.qml` 追加组件映射。
5. Python 脚本复用 `3rdparty/EasyTrain/src/python/task/` 的任务协议并上报状态。

不要新增任务快照、控制器包装层或数据库导出接口；需要的模型/数据输入在 GUI 线程构造为
`ModelTaskRequest`，耗时工作始终放到后台。
