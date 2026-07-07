# model 模块说明

## 模块定位

`src/model` 通过 `add_plugin_library(model)` 构建，实际目标为 `dltool_model`，头文件接口目标为 `dltool_model_header`，QML URI 为 `dltool.model`。该模块负责模型注册、模型记录管理、训练/测试参数管理、任务表管理、模型任务启动/停止、数据集导出配置，以及外部训练/预测脚本的进程生命周期和 TCP 状态通信。

主要依赖：

- Qt Core/Gui/Quick/Network 和 QML 注册机制。
- `quickui`、`dltool_common`、`dltool_core`、`dltool_ui`、`dltool_settings`、`dltool_database`、`dltool_data`。
- `yaml-cpp` 读写模型参数、任务配置和数据集清单。
- `spdlog` 记录 C++ 侧日志。

当前职责划分：

- `ModelManager` 是项目级模型列表模型，管理数据库模型记录、框架/模型注册表、懒加载模型实例和训练/验证/测试数据集选择 ViewModel，并编排外部脚本任务准备与运行。
- `TaskTableModel` 和 `TaskManager` 管任务表、任务状态、任务中心 QML 单例、任务去重、任务启动/停止/删除，以及 TCP 停止命令。
- `TaskCommunicationServer` 提供本机 TCP JSON 行协议，接收脚本上报的状态、进度、ETA 和日志事件，并向脚本下发命令。
- `ModelTaskTypes` 提供 QML/C++ 共用的强类型任务枚举，并统一任务 key、显示名、配置文件命名、日志命名和数据集导出需求。
- `ModelStorageService` 统一模型目录路径计算、目录创建和模型目录删除。
- `ModelDatasetSelection` 统一训练/验证/测试数据集选择快照、`dataset_selections` 序列化和恢复。
- `ModelTaskConfigService` 统一任务 YAML 读写、字段名和参数落盘。
- `ModelTaskPreparationService` 统一外部任务启动前的脚本选择、目录准备、选择快照生成、数据集导出、YAML 配置写入、Python 解释器解析和运行参数生成。
- `ExternalModelTaskRunner` 统一已准备外部 Python 进程的启动、环境变量、stdout/stderr 日志、停止/删除和退出状态映射。
- `IModel`/`IModelConfig`/`IParams`/`ParamGroupModel` 定义模型实例、参数配置和 QML 参数编辑数据模型。
- `ModelParamsSchema` 和 `YamlModel` 从 `config/models/<framework>/<model>.yaml` 加载参数 schema，并生成 `ITrainParams`/`ITestParams`。
- `ModelDatasetOrganizer` 在任务启动前按框架导出训练/验证/测试数据清单和派生 mask，任务类型判断来自 `ModelTaskTypes`。
- `qml/` 提供训练页、测试页、模型列表、模型创建、数据集选择、参数编辑和任务操作入口。

## 目录结构

- `CMakeLists.txt`
  声明 `model` 插件库、QML 模块和依赖库。
- `include/model/ModelManager.h`、`ModelManager.cpp`
  模型列表模型，负责数据库中的模型记录、框架/模型注册表、模型实例缓存、数据集 ViewModel 初始化和模型任务分发。
- `include/model/TaskManager.h`、`TaskManager.cpp`
  任务表模型和任务管理器。`TaskManager` 使用 `QT_QML_SINGLETON(TaskManager)` 注册为 QML 单例。
- `include/model/TaskCommunication.h`、`TaskCommunication.cpp`
  基于 TCP 的任务通信服务，接收训练/预测脚本上报的状态、进度、ETA，并向脚本发送停止命令。
- `include/model/ModelTaskTypes.h`、`ModelTaskTypes.cpp`
  模型任务类型描述，集中维护 `ModelTaskTypes::Type` 枚举、任务 key、显示名、日志名、配置名和是否需要导出数据集。
- `include/model/ModelStorageService.h`、`ModelStorageService.cpp`
  模型目录服务，集中维护 `models/<uuid>/` 及其 `configs`、`datasets`、`logs`、`results`、`weights` 子目录。
- `include/model/ModelDatasetSelection.h`、`ModelDatasetSelection.cpp`
  数据集选择快照，集中处理训练/验证/测试选择状态读取、YAML map 序列化和恢复。
- `include/model/ModelTaskConfigService.h`、`ModelTaskConfigService.cpp`
  任务配置服务，集中维护 YAML 字段名、`train.yaml`/`test.yaml` 读写和参数序列化。
- `include/model/ModelTaskPreparationService.h`、`ModelTaskPreparationService.cpp`
  外部任务准备服务，将模型任务请求转换为可直接启动的 `PreparedExternalModelTask`。
- `include/model/PreparedExternalModelTask.h`
  外部进程运行规格，包含 task id、程序、参数、工作目录、Python path 和日志路径。
- `include/model/ExternalModelTaskRunner.h`、`ExternalModelTaskRunner.cpp`
  外部模型任务运行器，集中维护已准备 Python 进程、运行环境、任务日志、停止请求和退出状态映射。
- `IModel.*`、`IModelConfig.h`
  模型实例、模型配置接口和训练/验证/测试数据集选择 ViewModel 挂载点。
- `IParams.*`、`ModelParamDefs.*`、`ModelParamsSchema.*`
  参数分组、参数字段、参数 schema 解析和 QML 参数编辑数据模型。
- `YamlModel.*`
  YAML 配置驱动的模型实现。
- `include/model/ModelDatasetOrganizer.h`、`ModelDatasetOrganizer.cpp`
  将数据集选择快照按框架需要导出为训练/验证/测试文件列表或 `manifest.yaml`，必要时生成派生标注文件。
- `include/model/Logger.h`、`Logger.cpp`
  注册模块级 `spdlog` logger，并设置默认 logger。
- `DetectionFramework.cpp`
  注册检测框架 `ultralytics`。当前只定义框架名，未在 C++ 注册训练/预测脚本路径。
- `AnomalyDetectionFramework.cpp`
  注册异常检测框架 `anomalib`。
- `FsSam2Framework.cpp`
  注册内部框架 `FS-SAM2`，覆盖检测、分割、异常检测方法，用于小样本学习流程，不在训练页面的模型创建 UI 中展示。
- `DetectionModels.cpp`
  注册检测模型 `YOLOv5`、`YOLOv8`。
- `AnomalyDetectionModels.cpp`
  注册异常检测模型 `patchcore`、`dinomaly2`。
- `model_system_architecture.drawio`
  模型系统结构图。
- `qml/`
  训练、测试、模型列表、模型创建弹窗、参数面板和训练面板。

## 注册机制

框架和模型按“框架/模型架构”组织。

框架注册使用 `DLT_REGISTER_FRAMEWORK`：

```cpp
DLT_REGISTER_FRAMEWORK(DeepLearningMethod::Detection, Ultralytics, ultralyticsFramework());
DLT_REGISTER_FRAMEWORK(DeepLearningMethod::AnomalyDetection, Anomalib, anomalibFramework());
DLT_REGISTER_FRAMEWORK(DeepLearningMethod::Detection, FsSam2Detection, fsSam2Framework());
DLT_REGISTER_FRAMEWORK(DeepLearningMethod::Segmentation, FsSam2Segmentation, fsSam2Framework());
DLT_REGISTER_FRAMEWORK(DeepLearningMethod::AnomalyDetection, FsSam2Anomaly, fsSam2Framework());
```

`ModelManager::FrameworkDefinition` 定义框架级信息：

- `name`：框架名，例如 `ultralytics`、`anomalib`、`FS-SAM2`。
- `root`：框架运行根目录。相对路径会按 `QCoreApplication::applicationDirPath()` 解析。
- `train_script`：训练脚本。相对路径会按 `root` 解析。
- `predict_script`：预测脚本。相对路径会按 `root` 解析。
- `scripts`：额外脚本，例如 `box_to_mask`。
- `python_paths`：启动脚本时追加到 `PYTHONPATH` 的路径。相对路径会按 `root` 解析，并保留原系统 `PYTHONPATH`。
- `visible_for_model_creation`：是否允许在训练页面模型创建 UI 中展示。

当前框架：

- `ultralytics`：注册到检测方法，当前只定义框架名，没有注册 `train_script`/`predict_script`。
- `anomalib`：注册到异常检测方法，`root = python/open-edge-platform/anomalib`，脚本为 `train.py` 和 `predict.py`。
- `FS-SAM2`：注册到检测、分割、异常检测方法，`root = python/fornib/FS-SAM2`，脚本为 `train.py`、`predict.py` 和额外 `box_to_mask.py`，但 `visible_for_model_creation = false`。

模型注册使用 `DLT_REGISTER_YAML_MODEL`：

```cpp
DLT_REGISTER_YAML_MODEL(DetectionMethod, YoloV5, "ultralytics", "YOLOv5");
DLT_REGISTER_YAML_MODEL(DetectionMethod, YoloV8, "ultralytics", "YOLOv8");
DLT_REGISTER_YAML_MODEL(AnomalyDetectionMethod, Patchcore, "anomalib", "patchcore");
DLT_REGISTER_YAML_MODEL(AnomalyDetectionMethod, Dinomaly2, "anomalib", "dinomaly2");
```

YAML 模型配置位于：

```text
config/models/<framework>/<model>.yaml
```

例如：

```text
config/models/ultralytics/YOLOv5.yaml
config/models/ultralytics/YOLOv8.yaml
config/models/anomalib/patchcore.yaml
config/models/anomalib/dinomaly2.yaml
```

## 参数系统

`ModelParamsSchema::loadModelParamsSchema(framework, architecture)` 会从应用目录的 `config/models/<framework>/` 查找模型 YAML。YAML 支持两种形态：

- 顶层就是模型节点，例如 `config/models/anomalib/patchcore.yaml`。
- 顶层用模型架构名包一层，例如 `config/models/ultralytics/YOLOv5.yaml` 中的 `YOLOv5:`。

模型节点字段：

- `framework`
- `model_architecture`
- `model_name`
- `method`
- `train_params`
- `test_params`

`train_params` 和 `test_params` 都是参数分组数组。每个分组会变成一个 `ParamGroupModel`，核心字段包括：

- `name_en`
- `name_cn`
- `description`
- `enabled`
- `part_index`
- `params`

每个参数会变成 `ParamDefinition`，核心字段包括：

- `name_en`
- `name_cn`
- `description`
- `value`
- `default_value`
- `value_type`
- `value_range`
- `control_type`
- `options`
- `enabled`
- `unit`

QML 参数面板按 `part_index` 拆成两列渲染。当前已使用的控件类型包括 `text`、`spin`、`slider`、`checkbox` 和 `combo`。

## 创建模型流程

1. QML 通过 `ModelManager.supportedFrameworks()` 获取当前项目任务类型下可创建的框架。
2. 用户选择框架后，通过 `ModelManager.supportedModelArchitectures(framework)` 获取该框架下的模型架构。
3. QML 调用 `ModelManager.addModel(name, framework, architecture)`。
4. `ModelManager` 校验名称、框架和模型架构非空，并确认 `framework + architecture` 已注册。
5. `ModelManager` 创建模型 uuid，通过 `ModelStorageService` 创建 `models/<uuid>/` 及子目录，然后写入数据库 `models` 表。
6. 打开模型时，`ModelManager.modelForUuid(uuid)` 根据 `framework_name + model_architecture` 懒创建并缓存模型实例。
7. `ModelManager` 为模型实例挂载训练、验证、测试数据集选择 ViewModel。
8. `YamlModel` 从 `config/models/<framework>/<model>.yaml` 构造训练/测试参数。
9. 如果存在历史 `configs/train.yaml` 或 `configs/test.yaml`，`ModelManager` 通过 `ModelTaskConfigService` 懒加载参数和 `dataset_selections`，恢复到内存模型。

`visible_for_model_creation = false` 的框架不会出现在模型创建 UI 中，例如 `FS-SAM2`。

复制模型时会新增数据库记录和模型目录。如果源模型实例已在内存中，复制出的模型实例会继承当前内存中的训练/测试参数值。

## 训练/测试任务流程

任务入口统一经过 `TaskManager`。

1. UI 调用 `TaskManager.startModelTask(model_uuid, model_name, ModelTaskTypes.Type)`。
2. `TaskManager` 检查任务表中是否已有同一模型和任务类型的非终态任务。
3. 如果没有任务，则 `TaskManager.addModelTask()` 创建任务记录。
4. `TaskManager.startTask(task_id)` 如果当前项目 `ModelManager` 能处理该模型任务，则转发给 `ModelManager.startTask(task_id)`。
5. `ModelManager` 查出模型实例和框架定义，并通过 `ModelTaskPreparationService::frameworkHasScript()` 判断是否存在外部脚本：
   - `ModelTaskTypes.Train` 使用 `train_script`。
   - `ModelTaskTypes.Test` 使用 `predict_script`。
   - `ModelTaskTypes.BoxToMask` 是普通任务表枚举，不由 `ExternalModelTaskRunner` 启动。
6. 如果框架没有为该任务定义脚本，`ModelManager.startTask()` 返回成功，任务只进入 `TaskTableModel` 的普通运行状态，不启动 Python 进程，也不生成任务配置。
7. 如果框架定义了脚本，`ModelManager` 构造 `ModelTaskPreparationService::Request` 并交给准备服务。
8. `ModelTaskPreparationService` 通过 `ModelStorageService` 确保 `models/<uuid>/configs`、`results`、`logs`、`weights`、`datasets` 目录存在。
9. `ModelTaskPreparationService` 调用 `ModelDatasetOrganizer` 将 UI 选择的数据集导出到 `models/<uuid>/datasets`。训练任务要求训练集非空，测试/预测任务要求测试集非空；验证集为空时会跳过。
10. `ModelTaskPreparationService` 通过 `ModelTaskConfigService` 写出 YAML 任务配置。当前配置文件名由 `ModelTaskTypes` 决定：训练写 `train.yaml`，测试/预测写 `test.yaml`；无配置文件名的任务不会作为外部模型脚本启动。
11. `ModelTaskPreparationService` 生成 `PreparedExternalModelTask`，`ExternalModelTaskRunner` 启动 Python 子进程，传入：
   - `--config`
   - `--dltool_task_host`
   - `--dltool_task_port`
   - `--dltool_task_id`
12. 子进程工作目录为框架 `root`，环境变量会设置 `PYTHONPATH`、`PYTHONUTF8=1`、`PYTHONUNBUFFERED=1`。
13. 子进程 stdout/stderr 统一写入 `models/<uuid>/logs/train.log`、`test.log` 或 `task.log`。
14. 外部脚本通过 TCP 向 `TaskManager` 上报状态、进度和 ETA。
15. `TaskManager` 更新 `TaskTableModel`，任务中心 UI 自动刷新。

注意：Python/Lightning 等库可能把普通运行信息和 warning 写到 stderr。`ExternalModelTaskRunner` 不从 stdout/stderr 转发界面日志，所有打印统一落到日志文件；界面错误只来自脚本 try/catch 后通过 TCP 上报的异常信息。

停止流程：

1. UI 调用 `TaskManager.stopTask(task_id)` 或 `TaskManager.stopModelTask(...)`。
2. `TaskManager` 通过 TCP 发送 `stop` 命令；如果脚本尚未绑定 task socket，会广播到当前连接。
3. `TaskManager` 转发给 `ModelManager.stopTask(task_id)`。
4. `ModelManager` 委托 `ExternalModelTaskRunner` 对外部进程先 `terminate()`，5 秒后仍未退出则 `kill()`。
5. `TaskManager` 更新任务状态并发出 `taskStopRequested`。

暂停只由 `TaskTableModel` 管理状态。定义了外部脚本的模型任务当前不支持暂停按钮；没有外部脚本的普通任务可以暂停/恢复。

## TaskManager

`TaskManager` 是 QML 单例，主要职责：

- 持有 `TaskTableModel`。
- 管理任务新增、启动、暂停、停止、完成、失败、删除。
- 持有 `TaskCommunicationServer`。
- 接收外部脚本 TCP 消息并更新任务表。
- 向外部脚本发送停止命令。
- 通过 `setModelManager(ModelManager*)` 绑定当前项目的 `ModelManager`，用于转发模型任务启动/停止；切换项目时会清空当前任务表。

`TaskTableModel` 的核心字段：

- `task_id`
- `model_uuid`
- `model_name`
- `task_type`，枚举值，显示文本由 `ModelTaskTypes` 转换
- `status`
- `created_at`
- `running_time`
- `eta`
- `progress`

内部还保存 `supports_pause`，用于计算 QML 暴露的：

- `can_start`
- `can_pause`
- `can_stop`
- `can_finish`

任务 ID 只在当前 `TaskTableModel` 生命周期内递增；任务表当前不做数据库持久化。

## ModelManager

`ModelManager` 是项目级对象，由 `project::Project` 创建。主要职责：

- 从项目数据库加载、添加、重命名、删除、复制模型记录。
- 按当前项目任务类型筛选可用框架和模型架构。
- 懒创建并缓存模型实例。
- 为模型实例绑定训练/验证/测试数据集选择 ViewModel。
- 懒加载历史任务配置，把参数和数据集选择恢复到内存模型。
- 判断模型任务是否由当前项目处理。
- 保存项目目录上下文，并对定义了外部脚本的任务构造 `ModelTaskPreparationService::Request`；框架未定义脚本时，只让任务进入普通任务状态。
- 将准备服务生成的 `PreparedExternalModelTask` 交给 `ExternalModelTaskRunner` 启动。

`ModelManager` 不持有 `TaskManager*` 成员。需要访问任务表或 TCP 服务时，直接使用 `TaskManager::getInstance()`。

外部进程不再由 `ModelManager` 自己维护。`ModelManager` 只持有一个 `ExternalModelTaskRunner`，具体的 `QProcess`、停止请求、日志文件和退出码映射都在运行器内部处理。

## 抽象服务

`ModelTaskTypes` 是任务语义入口。UI、任务表和 C++ 调用链传递 `ModelTaskTypes::Type` 枚举，不再传递任务类型字符串。当前枚举包括 `Unknown`、`Train`、`Test` 和 `BoxToMask`。它统一给出任务 key、显示名、配置文件名、日志文件名前缀和是否要求数据集导出。

`ModelStorageService` 是模型存储入口。它接收项目目录，计算模型根目录和 `configs`、`datasets`、`logs`、`results`、`weights` 子目录，负责创建完整模型目录和安全删除单个模型目录。它不持有项目数据库。

`ModelDatasetSelection` 是数据集选择入口。它从模型的训练/验证/测试选择 ViewModel 生成快照，并负责把快照序列化为 `dataset_selections` 或从历史配置恢复回 ViewModel。数据集导出和任务配置写入共用这一个快照入口。

`ModelTaskConfigService` 是任务 YAML 入口。它集中维护配置字段名，负责读取历史 `train.yaml`/`test.yaml`，并在任务启动前构造和写入当前任务配置。

`ModelTaskPreparationService` 是外部任务准备入口。它接收模型、任务类型、框架定义和项目目录上下文，负责脚本路径判断、任务通信服务启动检查、Python 环境解析、数据集选择快照生成、数据集导出、配置写入，并生成 `PreparedExternalModelTask`。

`ExternalModelTaskRunner` 是纯进程运行入口。它只接收 `PreparedExternalModelTask`，负责启动进程、设置环境变量、stdout/stderr 日志落盘、停止/删除处理，以及退出状态映射。进程正常退出码 `0` 视为完成，退出码 `2` 或显式停止请求视为停止，其他退出视为失败。

## 模型目录与配置

每个模型在项目目录下使用独立 uuid 目录。路径计算、目录创建和删除由 `ModelStorageService` 统一处理：

```text
<project_dir>/models/<uuid>/
  configs/
  datasets/
  logs/
  results/
  weights/
```

删除模型记录时会同时通过 `ModelStorageService` 删除对应的 `models/<uuid>/` 目录。

任务配置由 `ModelTaskConfigService` 使用 YAML 写入模型目录下的 `models/<uuid>/configs/train.yaml` 或 `models/<uuid>/configs/test.yaml`。配置包含模型 uuid、模型名、任务类型、框架、模型架构、模型目录、结果目录、日志目录、权重目录、数据集文件路径、数据集选择，以及训练/测试参数。

模型列表初始化时不批量读取任务配置。界面选中模型并创建模型实例时，`ModelManager` 才通过 `ModelTaskConfigService` lazy 读取 `configs/train.yaml` 和 `configs/test.yaml`，将其中的 `train_params`、`test_params` 应用到内存中的参数模型，并将 `dataset_selections` 恢复到训练、验证、测试数据集选择树。参数和数据集选择编辑只修改内存值；启动训练或测试任务时，`ModelTaskPreparationService` 才通过 `ModelTaskConfigService` 把当前内存参数和数据集选择写回对应 YAML 文件。

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

`dataset_ids` 表示完整勾选的数据集节点，`label_classes` 表示按数据集/类别单独勾选的子节点。启动任务前，`ModelTaskPreparationService` 会先生成 `ModelDatasetSelections` 快照，再交给 `ModelDatasetOrganizer` 生成框架需要的 `datasets` 导出结果。

## 数据集组织

`ModelDatasetOrganizer` 不直接读取模型或 UI 选择 ViewModel。它接收 `ModelDatasetExportRequest`，其中包含 `ModelDatasetSelections` 快照和 `IModelDatasetSource` 只读数据源，并按框架输出数据组织文件。训练/预测判断来自 `ModelTaskTypes`，因此数据集导出规则和任务配置文件命名使用同一套任务语义。

导出规则：

- 训练任务导出 `train`，验证集可选；验证集为空时跳过。
- 测试/预测任务导出 `test`。
- 图像文件不复制，只在清单中保存原始路径。
- 框架布局按 `framework_name` 小写匹配：`anomalib`、`ultralytics`、`fs-sam2`，其他框架使用通用布局。

通用布局、`ultralytics` 和 `FS-SAM2` 按 split 输出：

```text
models/<uuid>/datasets/<split>/manifest.yaml
```

`manifest.yaml` 记录图像和标注元数据，图像只保存原始路径，不复制图像文件。图像条目包含 `id`、`path`、`dataset_id`、`dataset_name`、`width`、`height`、图像级标签和 `labels`。

标注条目包含 `label_id`、`label_class_id`、`label_class_name`、`label_class_group`、`class_index` 和原始标注 `data`。框架可以追加自己的字段：

- `anomalib`
  不写 split 子目录的 `manifest.yaml`，而是输出 `models/<uuid>/datasets/train.yaml`、`validation.yaml`、`test.yaml` 作为文件列表，并输出共享 `models/<uuid>/datasets/masks/<id>.png`。文件列表包含 `samples` 和 `masks_dir`，sample 包含 `id`、`path`、`label_index`、`mask`；`mask` 只保存文件名。
- `ultralytics`
  标注条目额外输出归一化 `yolo` bbox，供后续检测脚本消费。
- `FS-SAM2`
  C++ 数据集组织阶段将 polygon/bbox 标注转换为 mask PNG，写入 `models/<uuid>/datasets/<split>/masks/`，并在 label 上写入 `mask_path`。Python 训练/预测脚本只读取 `mask_path`，不再执行 polygon 到 mask 的转换。

## TCP 任务协议

`TaskCommunicationServer` 监听 `127.0.0.1` 的随机端口，接收 JSON 行协议。外部脚本连接后，每条消息以换行结尾。无效 JSON 会被忽略并记录 warning。

核心字段由 `TaskProtocolField` 定义：

- `task_id`
- `type`
- `status`
- `progress`
- `eta_seconds`
- `message`
- `command`

支持的消息类型包括：

- `event`
- `status`
- `progress`
- `log`
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

外部训练/预测脚本应在启动后连接 `TaskManager` 提供的 host/port，并持续上报任务状态。服务端会用首个带 `task_id` 的消息建立 socket 与任务的映射；发送停止命令时优先发给该任务 socket，找不到映射时广播给所有连接。

## QML 入口

- `TrainPage.qml`
  训练页面，左侧模型列表，右侧 `TrainPanel`。
- `TestPage.qml`
  测试页面，左侧模型列表，右侧测试数据集和测试参数面板。
- `component/ModelView.qml`
  模型列表、模型选择、右键菜单、创建模型入口和任务开始/停止按钮。
- `component/ModelDelegate.qml`
  单个模型卡片，展示框架、架构、训练结果、测试结果和任务按钮。
- `component/ModelHeader.qml`
  模型列表标题和添加模型按钮。
- `component/ModelFormDialog.qml`
  创建模型，先选框架，再选模型架构。
- `component/ParamsForm.qml`
  训练参数主表单，组合数据集选择和两列参数面板。
- `component/ParamPanel.qml`
  参数分组和参数控件渲染。
- `component/DatasetPanel.qml`
  训练、验证数据集选择。
- `train/TrainPanel.qml`
  训练参数和训练结果 tab。

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

- `ModelManager` 负责模型记录、框架/模型注册、模型实例缓存、项目目录上下文、数据集 ViewModel 绑定和模型任务分发，不直接管理进程、目录细节或 YAML 字段。
- `TaskManager` 负责任务表和任务通信，不负责具体模型参数、目录、配置写入或进程启动细节。
- `ModelTaskPreparationService` 负责外部脚本选择、Python 解释器解析、任务通信参数、数据集导出编排和配置写入编排。
- `ExternalModelTaskRunner` 负责已准备外部进程、运行环境、任务日志、停止请求和退出状态映射。
- `ModelStorageService` 负责模型存储路径、目录创建和模型目录删除。
- `ModelDatasetSelection` 负责数据集选择快照、`dataset_selections` map 序列化和恢复。
- `ModelTaskConfigService` 负责任务配置字段、YAML 读写和参数序列化。
- `ModelTaskTypes` 负责任务类型枚举、任务 key、显示名、配置文件名、日志文件名前缀和数据集导出需求。
- `ModelDatasetOrganizer` 负责把数据集选择快照导出为框架可消费的数据清单和派生 mask，不负责读取 UI 模型、进程启动或任务配置写入。
- `IModelDatasetSource` 是数据集导出所需的只读数据接口，避免导出器依赖完整 `DataManager`。
- 框架层定义 root、脚本和运行环境。
- 模型层定义参数和模型架构。
- 原始数据集、标注编辑、图像导入导出属于 `data` 或 `feature`；模型任务启动前的数据集 manifest 组织属于 `model`。

## 扩展约定

新增框架：

1. 在对应业务文件中定义 `ModelManager::FrameworkDefinition`。
2. 使用 `DLT_REGISTER_FRAMEWORK` 注册。
3. 如需训练/预测脚本，定义 `root`、`train_script`、`predict_script` 和 `python_paths`；不定义脚本时，该框架任务只进入普通任务表状态，不会启动外部进程。

新增 YAML 模型：

1. 在 `config/models/<framework>/<model>.yaml` 添加参数配置。
2. 在对应 `*Models.cpp` 中使用 `DLT_REGISTER_YAML_MODEL` 注册。
3. 确保模型所属框架已经注册。

新增参数类型：

1. 扩展参数 schema。
2. 扩展 `ModelParamDefs` 或 YAML 解析。
3. 扩展 QML 参数渲染逻辑。

新增任务类型语义：

1. 在 `ModelTaskTypes::Type` 中新增枚举值。
2. 在 `describeModelTask()` 中定义任务 key、显示名、日志名和是否需要数据集导出。
3. 如果该任务需要任务配置文件，补充对应配置文件命名，并确认 `ModelTaskConfigService::write()` 能落盘。
4. 如果该任务需要导出数据集，扩展 `ModelDatasetOrganizer` 的 split 规则和校验逻辑。

新增外部脚本任务：

1. 训练任务放到 `train_script`，测试/预测任务放到 `predict_script`。新增外部脚本任务需要先扩展 `ModelTaskTypes::Type` 和 `ModelTaskPreparationService::scriptForTask()`。
2. 脚本支持 `--config`、`--dltool_task_host`、`--dltool_task_port`、`--dltool_task_id`。
3. 脚本通过 TCP 上报 `task_id`、`type`、`status`、`progress`、`eta_seconds` 和错误 `message`。
4. 脚本处理 `stop` 命令并正常退出；如以退出码 `2` 结束，`ExternalModelTaskRunner` 会把任务视为已停止。
