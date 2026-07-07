# model 模块说明

## 模块定位

`src/model` 构建目标为 `dltool_model`，QML URI 为 `dltool.model`。该模块负责模型注册、模型记录管理、训练/测试参数管理、任务表管理、模型任务启动/停止，以及外部训练/预测脚本的 TCP 状态通信。

当前职责划分：

- `ModelManager` 管模型列表、框架/模型注册、模型实例、外部训练/预测进程启动与停止。
- `TaskManager` 管任务表、任务状态、任务中心 QML 单例、TCP 通信和停止命令下发。
- `IModel`/`IModelConfig`/`IParams` 管模型实例和参数结构。
- `YamlModel` 从 `config/models/<framework>/<model>.yaml` 加载模型参数定义。
- `qml/` 提供训练页、测试页、模型创建、参数编辑和任务操作入口。

## 目录结构

- `include/model/ModelManager.h`、`ModelManager.cpp`
  模型列表模型，负责数据库中的模型记录，以及框架/模型注册表。
- `include/model/TaskManager.h`、`TaskManager.cpp`
  任务表模型和任务管理器。`TaskManager` 使用 `QT_QML_SINGLETON(TaskManager)` 注册为 QML 单例。
- `include/model/TaskCommunication.h`、`TaskCommunication.cpp`
  基于 TCP 的任务通信服务，接收训练/预测脚本上报的状态、进度、ETA，并向脚本发送停止命令。
- `IModel.*`、`IModelConfig.h`
  模型实例和模型配置接口。
- `IParams.*`、`ModelParamDefs.*`、`ModelParamsSchema.*`
  参数分组、参数字段、参数 schema 和 QML 参数编辑数据模型。
- `YamlModel.*`
  YAML 配置驱动的模型实现。
- `include/model/ModelDatasetOrganizer.h`、`ModelDatasetOrganizer.cpp`
  将 UI 选择的数据集按框架需要导出为训练/验证/测试文件列表或 `manifest.yaml`，必要时生成派生标注文件。
- `DetectionFramework.cpp`
  注册检测框架 `ultralytics`。
- `AnomalyDetectionFramework.cpp`
  注册异常检测框架 `anomalib`。
- `FsSam2Framework.cpp`
  注册内部框架 `FS-SAM2`，用于小样本学习流程，不在训练页面的模型创建 UI 中展示。
- `DetectionModels.cpp`
  注册检测模型 `YOLOv5`、`YOLOv8`。
- `AnomalyDetectionModels.cpp`
  注册异常检测模型 `patchcore`、`dinomaly2`。
- `qml/`
  训练、测试、模型列表、模型创建弹窗、参数面板和训练面板。

## 注册机制

框架和模型按“框架/模型架构”组织。

框架注册使用 `DLT_REGISTER_FRAMEWORK`：

```cpp
DLT_REGISTER_FRAMEWORK(DeepLearningMethod::Detection, Ultralytics, ultralyticsFramework());
DLT_REGISTER_FRAMEWORK(DeepLearningMethod::AnomalyDetection, Anomalib, anomalibFramework());
```

`ModelManager::FrameworkDefinition` 定义框架级信息：

- `name`：框架名，例如 `ultralytics`、`anomalib`、`FS-SAM2`。
- `root`：框架运行根目录。
- `train_script`：训练脚本。
- `predict_script`：预测脚本。
- `scripts`：额外脚本，例如 `box_to_mask`。
- `python_paths`：启动脚本时追加到 `PYTHONPATH` 的路径。
- `visible_for_model_creation`：是否允许在训练页面模型创建 UI 中展示。

模型注册使用 `DLT_REGISTER_YAML_MODEL`：

```cpp
DLT_REGISTER_YAML_MODEL(DetectionMethod, YoloV5, "ultralytics", "YOLOv5");
DLT_REGISTER_YAML_MODEL(AnomalyDetectionMethod, Patchcore, "anomalib", "patchcore");
```

YAML 模型配置位于：

```text
config/models/<framework>/<model>.yaml
```

例如：

```text
config/models/ultralytics/YOLOv5.yaml
config/models/anomalib/patchcore.yaml
```

## 创建模型流程

1. QML 通过 `ModelManager.supportedFrameworks()` 获取当前项目任务类型下可创建的框架。
2. 用户选择框架后，通过 `ModelManager.supportedModelArchitectures(framework)` 获取该框架下的模型架构。
3. QML 调用 `ModelManager.addModel(name, framework, architecture)`。
4. `ModelManager` 写入数据库 models 表，并在项目目录下创建 `models/<uuid>/`。
5. 打开模型时，`ModelManager` 根据 `framework_name + model_architecture` 创建注册模型实例。
6. `YamlModel` 从 `config/models/<framework>/<model>.yaml` 构造训练/测试参数。

`visible_for_model_creation = false` 的框架不会出现在模型创建 UI 中，例如 `FS-SAM2`。

## 训练/测试任务流程

任务入口统一经过 `TaskManager`。

1. UI 调用 `TaskManager.startModelTask(model_uuid, model_name, task_type)`。
2. `TaskManager` 检查任务表中是否已有同一模型和任务类型的非终态任务。
3. 如果没有任务，则 `TaskManager.addModelTask()` 创建任务记录。
4. `TaskManager.startTask(task_id)` 转发给当前项目的 `ModelManager.startTask(task_id)`。
5. `ModelManager` 根据模型所属框架查找训练/预测脚本。
6. `ModelManager` 确保 `models/<uuid>/results`、`logs`、`weights`、`datasets` 目录存在。
7. `ModelManager` 调用 `ModelDatasetOrganizer` 将 UI 选择的数据集导出到 `models/<uuid>/datasets`。
8. `ModelManager` 写出 YAML 任务配置，并启动 Python 子进程，传入：
   - `--config`
   - `--dltool_task_host`
   - `--dltool_task_port`
   - `--dltool_task_id`
9. 子进程 stdout/stderr 统一写入 `models/<uuid>/logs/train.log` 或 `models/<uuid>/logs/test.log`。
10. 外部脚本通过 TCP 向 `TaskManager` 上报状态、进度和 ETA。
11. `TaskManager` 更新 `TaskTableModel`，任务中心 UI 自动刷新。

注意：Python/Lightning 等库可能把普通运行信息和 warning 写到 stderr。`ModelManager` 不再从 stdout/stderr 转发界面日志，所有打印统一落到日志文件；界面错误只来自脚本 try/catch 后通过 TCP 上报的异常信息。

停止流程：

1. UI 调用 `TaskManager.stopTask(task_id)` 或 `TaskManager.stopModelTask(...)`。
2. `TaskManager` 通过 TCP 发送 `stop` 命令。
3. `TaskManager` 转发给 `ModelManager.stopTask(task_id)` 停止外部进程。
4. `TaskManager` 更新任务状态并发出 `taskStopRequested`。

## TaskManager

`TaskManager` 是 QML 单例，主要职责：

- 持有 `TaskTableModel`。
- 管理任务新增、启动、暂停、停止、完成、失败、删除。
- 持有 `TaskCommunicationServer`。
- 接收外部脚本 TCP 消息并更新任务表。
- 向外部脚本发送停止命令。
- 通过 `setModelManager(ModelManager*)` 绑定当前项目的 `ModelManager`，用于转发模型任务启动/停止。

`TaskTableModel` 的核心字段：

- `task_id`
- `model_uuid`
- `model_name`
- `task_type`
- `status`
- `created_at`
- `running_time`
- `eta`
- `progress`
- `supports_pause`

## ModelManager

`ModelManager` 是项目级对象，由 `project::Project` 创建。主要职责：

- 从项目数据库加载、添加、重命名、删除、复制模型记录。
- 按当前项目任务类型筛选可用框架和模型架构。
- 创建模型实例并绑定训练/验证/测试数据集选择 ViewModel。
- 维护外部训练/预测进程。
- 根据框架定义启动训练/预测脚本。

`ModelManager` 不持有 `TaskManager*` 成员。需要访问任务表或 TCP 服务时，直接使用 `TaskManager::getInstance()`。

## 模型目录与配置

每个模型在项目目录下使用独立 uuid 目录：

```text
<project_dir>/models/<uuid>/
  configs/
  datasets/
  logs/
  results/
  weights/
```

删除模型记录时会同时删除对应的 `models/<uuid>/` 目录。

任务配置使用 YAML 写入模型目录下的 `models/<uuid>/configs/train.yaml` 或 `models/<uuid>/configs/test.yaml`。配置包含模型 uuid、框架、模型架构、模型目录、结果目录、日志目录、权重目录、数据集文件路径，以及训练/测试参数。

模型列表初始化时不批量读取任务配置。界面选中模型并创建模型实例时，`ModelManager` 才 lazy 读取 `configs/train.yaml` 和 `configs/test.yaml`，将其中的 `train_params`、`test_params` 应用到内存中的参数模型，并将 `dataset_selections` 恢复到训练、验证、测试数据集选择树。参数和数据集选择编辑只修改内存值；启动训练或测试任务时，`ModelManager` 才把当前内存参数和数据集选择写回对应 YAML 文件。

`dataset_selections` 保存 UI 勾选状态，不是框架训练脚本直接消费的数据清单：

```yaml
dataset_selections:
  train:
    dataset_ids: [1]
    label_classes:
      - dataset_id: 2
        label_class_id: 5
  validation:
    dataset_ids: []
    label_classes: []
  test:
    dataset_ids: [3]
    label_classes: []
```

`dataset_ids` 表示完整勾选的数据集节点，`label_classes` 表示按数据集/类别单独勾选的子节点。启动任务前，`ModelDatasetOrganizer` 仍会基于这些内存选择生成框架需要的 `datasets` 导出结果。

## 数据集组织

`ModelDatasetOrganizer` 在启动训练/测试前读取模型上的训练、验证、测试数据集选择 ViewModel，并按框架输出数据组织文件。

通用布局按 split 输出：

```text
models/<uuid>/datasets/<split>/manifest.yaml
```

`manifest.yaml` 记录图像和标注元数据，图像只保存原始路径，不复制图像文件。图像条目包含 `id`、`path`、`dataset_id`、`dataset_name`、`width`、`height`、图像级标签和 `labels`。

标注条目包含 `label_id`、`label_class_id`、`label_class_name`、`label_class_group`、`class_index` 和原始标注 `data`。框架可以追加自己的字段：

- `anomalib`
  输出 `models/<uuid>/datasets/train.yaml`、`validation.yaml`、`test.yaml` 作为文件列表，并输出共享 `models/<uuid>/datasets/masks/<id>.png`。文件列表的 sample 包含 `id`、`path`、`label_index`、`mask`；`mask` 只保存文件名，由脚本结合 `masks_dir` 读取。训练脚本使用 normal 训练样本，验证/测试读取对应文件列表并通过 TCP 上报训练、验证、测试状态。
- `ultralytics`
  标注条目额外输出归一化 `yolo` bbox，供后续检测脚本消费。
- `FS-SAM2`
  C++ 数据集组织阶段将 polygon/bbox 标注转换为 mask PNG，写入 `models/<uuid>/datasets/<split>/masks/`，并在 label 上写入 `mask_path`。Python 训练/预测脚本只读取 `mask_path`，不再执行 polygon 到 mask 的转换。

## TCP 任务协议

`TaskCommunicationServer` 接收 JSON 行协议。核心字段由 `TaskProtocolField` 定义：

- `task_id`
- `type`
- `status`
- `progress`
- `eta_seconds`
- `message`
- `command`

支持的状态包括：

- `pending`
- `running`
- `paused`
- `stopped`
- `finished`
- `failed`
- `error`

当前支持的命令：

- `stop`

外部训练/预测脚本应在启动后连接 `TaskManager` 提供的 host/port，并持续上报任务状态。

## QML 入口

- `TrainPage.qml`
  训练页面，使用 `TaskManager` 单例启动/停止训练任务。
- `TestPage.qml`
  测试页面，使用 `TaskManager` 单例启动/停止测试任务。
- `component/ModelView.qml`
  模型列表、模型操作和任务操作入口。
- `component/ModelFormDialog.qml`
  创建模型，先选框架，再选模型架构。
- `component/ParamPanel.qml`
  参数编辑面板。
- `train/TrainPanel.qml`
  训练配置面板。

## 与其他模块的关系

- `project`
  创建项目级 `ModelManager`，并将其绑定到 `TaskManager` 单例。
- `database`
  持久化模型记录。
- `data`
  提供训练、验证、测试数据集选择 ViewModel。
- `settings`
  提供 Python 环境路径等运行设置。
- `tool`
  任务中心窗口读取 `TaskManager.tasks`。
- `feature`
  小样本学习使用内部框架 `FS-SAM2`，并通过 `TaskManager` 加入任务中心。

## 边界定义

- `ModelManager` 负责模型记录和模型任务进程，不负责 TCP 通信。
- `TaskManager` 负责任务表和任务通信，不负责具体模型参数或进程启动细节。
- 框架层定义 root、脚本和运行环境。
- 模型层定义参数和模型架构。
- 原始数据集、标注编辑、图像导入导出属于 `data` 或 `feature`；模型任务启动前的数据集 manifest 组织属于 `model`。

## 扩展约定

新增框架：

1. 在对应业务文件中定义 `ModelManager::FrameworkDefinition`。
2. 使用 `DLT_REGISTER_FRAMEWORK` 注册。
3. 如需训练/预测脚本，定义 `root`、`train_script`、`predict_script` 和 `python_paths`。

新增 YAML 模型：

1. 在 `config/models/<framework>/<model>.yaml` 添加参数配置。
2. 在对应 `*Models.cpp` 中使用 `DLT_REGISTER_YAML_MODEL` 注册。
3. 确保模型所属框架已经注册。

新增参数类型：

1. 扩展参数 schema。
2. 扩展 `ModelParamDefs` 或 YAML 解析。
3. 扩展 QML 参数渲染逻辑。

新增外部脚本任务：

1. 在框架定义中加入脚本路径。
2. 脚本支持 `--config`、`--dltool_task_host`、`--dltool_task_port`、`--dltool_task_id`。
3. 脚本通过 TCP 上报状态和进度。
4. 脚本处理 `stop` 命令并正常退出。
