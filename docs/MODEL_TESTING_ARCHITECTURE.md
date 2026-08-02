# 模型测试任务与测试评估架构设计

> 状态：设计稿，暂不实施代码修改  
> 日期：2026-07-31  
> 适用范围：`src/model/`、模型运行任务、测试结果持久化及测试页面 QML

## 1. 背景

当前模型测试页面以模型为唯一作用域：

- 一个模型只有一份 `ITestParams`；
- 一个模型只有一个 `testDatasetViewModel`；
- 测试状态统一写入 `extra_data.test`；
- `TaskManager` 只使用 `(model_uuid, task_type)` 查找任务；
- 测试配置固定保存到 `configs/test.yaml`；
- 测试预测结果统一写入 `results/pred/`；
- `TestEvaluationPanel.qml` 目前只是空白容器。

这种结构无法表示同一个模型的多个独立测试，也无法让每个测试保存自己的参数、数据集、预测结果和评估报告。评估页面如果直接使用全局 `DataManager.labelInstances`，还会把测试预测和项目数据库中的 GT 标注混在一起，无法正确表达匹配对、FP 和 FN。

本设计同时解决以下问题：

1. 测试任务的创建、切换和独立持久化；
2. 训练、测试目录的重新划分；
3. 运行任务对具体测试任务的准确归属；
4. 测试指标、图表、混淆矩阵、实例缩略图和实例详情的统一展示；
5. 异常检测、目标检测、分割、分类等方法的评估扩展；
6. 旧项目目录和旧测试配置的兼容迁移。

## 2. 设计目标

### 2.1 功能目标

- 同一模型允许创建多个测试任务；
- 每个测试任务独立保存配置、数据集选择、结果和评估报告；
- 测试任务可以通过下拉列表切换；
- 运行任务中心可以区分同一模型的不同测试任务；
- 训练权重保存到 `train/weights`；
- 训练日志保存到 `train/logs`；
- 测试日志保存到 `test/logs`；
- 测试预测结果保存到 `test/<测试任务名称>/pred`；
- 测试评估面板可以显示实例级、图像级指标和方法特有图表；
- 混淆矩阵、实例网格和实例详情保持双向联动；
- QML 不绑定具体算法业务逻辑。

### 2.2 非目标

本阶段不要求：

- 将测试预测写回项目标注数据库；
- 在评估页面直接编辑 GT 或 PRED；
- 设计跨模型的测试任务；
- 将所有原始预测数据存入 SQLite；
- 在 QML 中计算 IoU、匹配关系或指标。

## 3. 核心原则

### 3.1 测试任务定义和运行实例分离

“测试任务”表示用户长期保存的一个具体测试；“运行实例”表示该测试的一次执行。

- 测试任务定义持久存在，可以被反复运行；
- 每次运行由 `TaskManager::task_id` 管理实时状态和开始结束时间；
- 同一测试任务同一时刻最多存在一个活动运行实例；
- 切换测试任务不会停止已启动的运行实例。

运行实例不需要持久 UUID，也不维护历史运行版本。`task_id` 只用于当前进程内查找、进度同步和 TCP 通信；重新启动应用后，页面依据当前测试任务的 `pred/`、`evaluation/` 和 `result.yaml` 恢复最近一次有效结果。

### 3.2 路径只由路径服务生成

其他模块不得自行拼接 `train`、`test`、`weights`、`logs` 或 `pred`。配置服务、任务控制器、Python 启动器和 QML 都只能消费路径服务生成的结果。

### 3.3 评估计算和评估展示分离

- Python/框架执行侧只负责推理并生成规范化 PRED 清单和预测文件；
- C++ `ModelEvaluationService` 负责读取 GT/PRED、匹配、指标、矩阵、图表计算及报告落盘；
- C++ ViewModel 负责加载、校验、应用过滤条件和转换成 Qt Model；
- QML 只负责展示和交互；
- 算法差异通过评估适配器和图表描述表达，不通过 QML 中的 `if (method === ...)` 表达。

评估服务在后台线程处理不依赖 `QObject`、`QModelIndex` 的纯值记录，计算完成后只在 GUI 线程提交 Qt Model 更新。第三方框架自带的指标可以作为补充字段保存，但不能替代本系统统一的 C++ 评估口径。

系统同时区分两类指标：

- `diagnostic_metrics`：由本系统工作点匹配事件聚合，用于混淆矩阵、实例联动和错误诊断；
- `official_metrics`：由方法适配器按该方法的正式规则计算，例如目标检测按类别、置信度和 IoU 规则计算 PR/mAP。

两类指标不得混用。顶部卡片显示哪一种由报告的 `primary_metric_set` 明确指定，并在标题或 tooltip 显示统计口径。

### 3.4 GT 和 PRED 不进入同一个标注模型

现有 `LabelInstancesGridView.qml` 使用的是项目数据库标注模型，适合标注复核，不适合测试结果。测试评估必须使用独立的评估事件模型：

- GT 只引用本次运行固化的数据集记录和项目标注；
- PRED 只引用当前测试任务的预测结果；
- 匹配对、FP、FN 都作为评估事件存在；
- 不修改项目数据库中的真实标注。

### 3.5 唯一事实来源

| 数据 | 唯一事实来源 |
|---|---|
| 测试任务 UUID、名称、目录名和顺序 | `test/tasks.yaml` |
| 当前可编辑测试配置 | `test/<任务名>/config.yaml` |
| 当前预测实际使用的推理配置 | `test/<任务名>/pred/config.yaml` |
| 当前评估实际使用的评估配置 | `test/<任务名>/evaluation/report.yaml` 内的 `evaluation_config` |
| 当前有效完整预测 | `test/<任务名>/pred/` |
| 当前有效评估明细 | `test/<任务名>/evaluation/` |
| 当前有效结果摘要和提交标志 | `test/<任务名>/result.yaml` |
| 实时状态、阶段和进度 | `TaskManager` |
| `extra_data.test_tasks` | 仅为可重建的 UI 加载缓存，不作为事实来源 |

测试任务的 `config.yaml` 可以在运行后继续编辑，因此它不能用于解释已有结果。当前预测由 `pred/config.yaml` 解释，当前评估由 `evaluation/report.yaml` 内的评估配置和 digest 解释。这样只修改评估参数时可以复用 PRED 重算评估，不必重新推理。

## 4. 最终目录结构

```text
models/<模型名称>/
├─ storage.yaml
├─ train/
│  ├─ config.yaml
│  ├─ datasets/
│  │  └─ datasets.yaml
│  ├─ weights/
│  └─ logs/
│     ├─ train.log
│     └─ tensorboard/
└─ test/
   ├─ tasks.yaml
   ├─ logs/
   │  └─ <测试任务UUID>.log
   ├─ <测试任务名称A>/
   │  ├─ config.yaml
   │  ├─ result.yaml
   │  ├─ datasets/
   │  │  └─ datasets.yaml
   │  ├─ pred/
   │  │  ├─ config.yaml
   │  │  ├─ images.txt
   │  │  ├─ manifest.yaml
   │  │  └─ <预测文件>
   │  └─ evaluation/
   │     └─ report.yaml
   └─ <测试任务名称B>/
      └─ ...
```

文件职责：

| 文件 | 职责 |
|---|---|
| `storage.yaml` | 模型目录结构版本及迁移状态 |
| `test/tasks.yaml` | 测试任务索引、顺序和上次选中任务 |
| `<任务>/config.yaml` | 当前可编辑的测试参数和单一测试数据集选择 |
| `<任务>/pred/config.yaml` | 当前 PRED 实际使用的推理配置、checkpoint 和推理摘要 |
| `<任务>/pred/images.txt` | 当前实际完成推理的全部图像；每行为 `image_id,image_path`，无预测实例的图像也必须存在 |
| `<任务>/result.yaml` | 最近一次有效运行的轻量摘要，也是结果提交完成标志 |
| `<任务>/pred/` | 当前有效的完整预测及规范化预测清单 |
| `evaluation/report.yaml` | 评估配置、digest、指标、混淆矩阵、图表、图像记录和实例事件 |

`pred/images.txt` 明确记录当前测试的图像全集，`pred/manifest.yaml` 记录完整预测及 score；二者共同界定当前 PRED，不能通过扫描预测文件猜测测试范围。`images.txt` 使用 UTF-8，首行为 `image_id,image_path`，后续按 CSV 转义规则保存路径。图像级评估记录和实例事件由 C++ 根据图像清单、GT 和 PRED 构建并写入 `report.yaml`。

每个测试任务只保留一份 PRED 和一份评估结果，不建立运行历史。需要重新推理时，控制器先安全确认任务根目录，再删除并重建该任务的 `pred/`、`evaluation/` 和 `result.yaml`；失败后页面显示当前运行失败且无有效结果。只需重新评估时保留 `pred/`，仅清理并重建 `evaluation/` 和 `result.yaml`。

日志也不建立运行 ID：`train/logs/train.log` 表示当前一次训练日志，`test/logs/<测试任务UUID>.log` 表示该测试任务当前一次运行日志，开始新运行时截断重写。多个测试任务并行时因 UUID 不同不会互相覆盖。

模型产物路径（配置、权重、PRED、评估和日志）在持久元数据、`report.yaml` 和结果摘要中统一保存为测试任务根目录或报告所在 `evaluation/` 目录可解析的相对路径。仅传给 Python 的当前进程参数可以展开为绝对路径。解析产物相对路径时必须规范化并验证最终路径仍位于对应模型或测试任务根目录内，拒绝越界引用。`images.txt` 中的源图路径是例外：以 `image_id` 为主键，`image_path` 沿用项目数据库的规范路径表示，可为项目相对路径或规范化的外部绝对路径。

## 5. 领域对象

### 5.1 测试任务定义

建议新增值对象：

```cpp
struct ModelTestTaskDefinition
{
    QString uuid;
    QString model_uuid;
    QString name;
    QString directory_name;
    QVariantMap test_params;
    ModelDatasetSelection dataset_selection;
    qint64 created_at{0};
    qint64 modified_at{0};
};
```

字段约束：

- `uuid` 是稳定标识，重命名后不改变；
- `name` 是用户可见名称；
- `directory_name` 是通过 Windows 路径校验的名称；
- 同一模型下名称大小写不敏感且不可重复；
- 测试参数和单一测试数据集选择属于测试任务，不再属于模型级活动状态；
- 普通模型测试只使用一个 `ModelDatasetSelection`。小样本学习拥有独立 feature 流程，不进入普通测试任务的数据选择或评估协议。

### 5.2 运行任务

现有 `TaskManager::Task` 建议增加：

```cpp
QString scope_uuid;
QString scope_name;
QString display_name;
QString phase;
QString config_path;
QString log_path;
```

保留现有 `task_id`，不新增持久运行标识。`phase` 用于表达同一个测试任务内部的推理、C++ 评估和结果写入阶段。

任务唯一作用域调整为：

```text
(model_uuid, task_type, scope_uuid)
```

约定：

- 训练任务 scope 固定为 `train`；
- 测试页面任务 scope 为测试任务 UUID；
- 小样本学习等组合流程继续使用独立的 feature scope，不复用普通测试任务定义；
- 不允许不带 scope 的测试页面任务与具体测试任务共享运行记录。

### 5.3 评估报告

评估报告是算法执行结果和 UI 之间的稳定协议。建议包含：

```yaml
schema_version: 3
model_uuid: "..."
test_task_uuid: "..."
method: object_detection
primary_metric_set: official_metrics
inference_digest: "sha256:..."
evaluation_digest: "sha256:..."
weight_digest: "sha256:..."

class_catalog:
  - id: 1
    name: 缺陷
    color: "#ff5252"

evaluation_config:
  confidence_threshold: 0.5
  iou_threshold: 0.5
  image_threshold: 0.5
  matching_strategy: hungarian_iou

diagnostic_metrics:
  instance:
    overall:
      average: micro
      precision: 0.91
      recall: 0.87
      f1: 0.89
      tp: 100
      fp: 10
      fn: 15
    per_class: []
  image:
    precision: 0.94
    recall: 0.90
    f1: 0.92
    tp: 50
    fp: 3
    fn: 5

official_metrics: {}
image_metric_definition:
  sample_unit: image_class_presence
  aggregation: micro

confusion_matrix: {}
charts: []
dataset_manifest: ../datasets/test/manifest.yaml
prediction_manifest: ../pred/manifest.yaml
image_list: ../pred/images.txt
instance_records: []
```

类别 ID、名称和颜色以报告中的 `class_catalog` 为当前评估结果的固定解释，不能在加载结果时直接套用项目当前已变化的类别名称或颜色。`inference_digest`、`evaluation_digest` 和 `weight_digest` 用于分别判断是否需要重新推理、只需重新评估，或结果仍然有效。

### 5.4 评估事件

实例网格中的一项不是单独的 GT 或 PRED，而是一个评估事件：

```yaml
event_uuid: "..."
image_id: 101
image_name: image_001.png
image_path: "..."
image_width: 1920
image_height: 1080
dataset_id: 7
image_tags: [3, 8]
status: true_positive
gt:
  instance_id: 501
  label_id: 9001
  class_id: 1
  class_name: 缺陷
  geometry: {}
  bounds: {}
pred:
  instance_id: pred-001
  class_id: 1
  class_name: 缺陷
  score: 0.96
  geometry: {}
  bounds: {}
match:
  iou: 0.83
  threshold: 0.5
crop_bounds: {}
```

`status` 最少支持：

| 状态 | 含义 |
|---|---|
| `true_positive` | GT 和 PRED 匹配且类别正确 |
| `class_mismatch` | 几何匹配，但预测类别错误 |
| `false_positive` | PRED 未匹配任何 GT |
| `false_negative` | GT 未匹配任何 PRED |
| `ignored` | 因忽略区域或评估规则排除，不进入常规统计 |

几何协议必须在 schema 中固定：bbox 统一使用绝对像素 `xywh`，并显式声明 `coordinate_system: image_pixels`；polygon 是绝对像素点数组；mask 使用文件引用或约定编码；旋转框使用 `cx/cy/width/height/angle_degrees`。`bounds` 与 `crop_bounds` 均处于原图绝对像素坐标系，并裁剪到 `[0, image_width] × [0, image_height]`。适配器负责把框架原始格式转换成该协议，QML 不猜测坐标格式。

## 6. 存储和任务管理服务

### 6.1 路径服务

新增：

```cpp
struct ModelTaskPaths
{
    QString model_root;
    QString task_root;
    QString editable_config_path;
    QString dataset_dir;
    QString weight_dir;
    QString log_dir;
    QString log_path;
    QString result_path;
    QString prediction_dir;
    QString prediction_config_path;
    QString prediction_images_path;
    QString prediction_manifest_path;
    QString evaluation_dir;
    QString evaluation_config_path;
    QString evaluation_report_path;
    QString evaluation_instances_path;
};
```

```cpp
class ModelTaskStorageService
{
public:
    ModelTaskPaths trainPaths(...);
    ModelTaskPaths testPaths(...);
    bool ensureTrainStorage(...);
    bool ensureTestTaskStorage(...);
};
```

测试任务使用训练权重：

```text
weight_dir = models/<模型>/train/weights
```

测试任务的 `task_root` 和 `prediction_dir` 分别为：

```text
task_root      = models/<模型>/test/<任务名称>
prediction_dir = models/<模型>/test/<任务名称>/pred
```

### 6.2 测试任务仓库

新增 `ModelTestTaskRepository`，只负责文件持久化：

```cpp
QList<ModelTestTaskDefinition> listTasks(...);
std::optional<ModelTestTaskDefinition> loadTask(...);
QString createTask(...);
bool saveTask(...);
bool renameTask(...);
bool removeTask(...);
bool writeResult(...);
```

写入要求：

- YAML 索引和结果提交标志使用原子写入；
- 创建时先写任务目录和配置，再提交 `tasks.yaml`；
- 重命名时先校验目标，再移动目录并更新索引和配置；
- Windows 下仅改变名称大小写时，先移动到同一父目录内的唯一临时名，再移动到目标名；
- 任何一步失败都应回滚或保持旧索引有效；
- 所有相对路径解析后必须验证仍位于任务根目录，拒绝路径穿越；
- 运行中的测试任务及所属模型禁止重命名和删除。

### 6.3 测试任务 QML 模型

新增项目级 `ModelTestTaskManager : QAbstractListModel`：

```cpp
Q_PROPERTY(QString modelUuid ...)
Q_PROPERTY(int currentIndex ...)
Q_PROPERTY(QString currentTaskUuid ...)
Q_PROPERTY(QString currentTaskName ...)
Q_PROPERTY(ITestParams *currentTestParams ...)
Q_PROPERTY(QObject *currentDatasetViewModel ...)
Q_PROPERTY(ModelEvaluationViewModel *currentEvaluation ...)
Q_PROPERTY(int count ...)
Q_PROPERTY(bool currentTaskRunning ...)
Q_PROPERTY(int currentTaskProgress ...)
```

```cpp
Q_INVOKABLE QString createTask(const QString &name);
Q_INVOKABLE bool switchTask(const QString &uuid);
Q_INVOKABLE bool renameTask(const QString &uuid, const QString &name);
Q_INVOKABLE bool deleteTask(const QString &uuid);
Q_INVOKABLE bool saveCurrentTask();
Q_INVOKABLE QString validateTaskName(const QString &name);
```

每个测试任务拥有：

- 从模型参数模板克隆的独立 `ITestParams`；
- 独立 `DataSelectionTreeModel`；
- 独立评估 ViewModel；
- 独立 dirty 状态。

参数或数据集改变后使用短延迟自动保存；切换任务、开始运行、关闭项目之前必须同步 flush。保存失败时取消切换或启动。

### 6.4 项目生命周期

`Project` 创建并持有：

- `ModelTestTaskRepository`；
- `ModelTestTaskManager`；
- `ModelTaskController`。

`ModelTaskController` 和 `ModelTestTaskManager` 使用同一个 Repository，保证从测试页面或任务中心启动时读取的是同一份测试任务定义。

生命周期约束：

- 删除测试任务前清理其已结束的 `TaskManager` 记录；活动任务则拒绝删除；
- 模型复制默认只复制模型定义和训练配置，不复制 `test/<任务>/pred`、评估结果或测试日志；是否复制 `train/weights` 由复制对话框单独选择；
- 项目关闭前停止 Python 任务、取消 C++ 评估、等待工作线程退出并 flush 当前配置；
- `ModelView` 中现有模型级“测试”入口应移除，或改为打开当前测试任务，不能继续启动无 `test_task_uuid` 的模糊测试；
- 运行中禁止重命名或删除模型、当前测试任务，防止输出路径失效。

## 7. 测试任务交互

### 7.1 TestTaskPanel

建议布局：

```text
测试任务：[测试任务下拉列表              ] [＋] [开始] [停止]
状态：运行中                           进度：35%
```

行为：

- 切换模型后加载该模型的任务列表；
- 新模型首次进入测试页面时自动创建“测试 1”；
- 旧模型迁移后自动选中“默认测试”；
- 点击 `＋` 弹出名称输入框；
- 创建成功后自动切换到新任务；
- 运行中的任务参数和数据集只读；
- 允许切换查看其他任务，后台任务继续运行；
- 任务状态和进度直接来自 `TaskManager`，不以 QML 本地状态为准。

`TestPage.qml` 是模型与测试任务上下文的唯一绑定入口，必须把当前模型和对应 `ModelTestTaskManager` 显式传给 `TestPanel`。`TestPanel` 内的三个区域始终绑定同一个 current task：

```text
TestPage.currentModelUuid
    -> ModelTestTaskManager.modelUuid/currentTaskUuid
        -> TestTaskPanel      （任务列表、状态、开始/停止）
        -> TestDatasetPanel   （currentDatasetViewModel）
        -> TestEvaluationPanel（currentEvaluation）
```

切换任务时由 Manager 一次性切换参数、数据集和评估 ViewModel，再发出 current task 变化；`TestDatasetPanel` 的饼图只消费当前数据选择 Model 的统计 roles。QML 不缓存另一份数据集选择，也不从 `TestPage` 分别拼接三个可能不一致的对象。

创建、切换和删除的边界行为：

- `＋` 创建前由 C++ 校验名称，成功写入目录和 `tasks.yaml` 后才加入下拉列表并切换；
- 切换前同步保存当前任务；保存失败则保留原选中项并显示错误；
- 删除当前任务后选择下一项，没有下一项则选择上一项；若删到零个任务，立即创建新的“测试 1”；
- 运行中的任务不可删除或重命名，但允许切换查看；
- 开始按钮根据摘要显示“开始测试”“重新推理”或“重新评估”，最终决策仍由 C++ 控制器校验，QML 不自行比较参数。

### 7.2 名称和目录规则

- 去除首尾空格；
- 建议长度 1～64；
- 禁止 `<>:"/\\|?*`；
- 禁止末尾句点或空格；
- 禁止 `CON`、`PRN`、`AUX`、`NUL`、`COM1` 等 Windows 保留名称；
- 同一模型下不允许大小写不敏感的重名；
- 显示名称经过校验后直接作为目录名；
- UUID 不随重命名变化；
- 测试日志按 UUID 命名，因此重命名不会丢失日志关联。

### 7.3 启动流程

```text
保存当前测试任务
  -> 创建带 scope_uuid 的运行任务
  -> 读取当前测试参数和单一数据集选择
  -> 生成 ModelTaskPaths
  -> 判断需要“重新推理”还是“仅重新评估”
  -> 必要时清空 pred/evaluation/result 并启动 Python
  -> 否则保留 pred，只清空 evaluation/result
  -> C++ 生成并校验评估结果
  -> 最后原子写入 result.yaml
  -> TaskManager 进入 Finished
  -> 加载 currentEvaluation
```

建议新增明确接口：

```cpp
int startTrainTask(const QString &model_uuid);
int startTestTask(const QString &model_uuid, const QString &test_task_uuid);
bool stopTestTask(const QString &model_uuid, const QString &test_task_uuid);
```

测试页面不得调用缺少 `test_task_uuid` 的测试启动接口。

控制器通过两类摘要自动决定执行范围：

| 变化 | 处理 |
|---|---|
| 数据集选择、实际图像列表、checkpoint、预处理、模型推理参数、预测最低保留分数 | 删除旧 PRED，重新运行 Python，再评估 |
| confidence、IoU、图像阈值、匹配策略、指标聚合方式、GT 标注内容或类别定义 | 保留 PRED，只由 C++ 重新评估 |
| 仅 `GlobalFilter` 的数据集、标签、Tag、文件名等页面过滤条件 | 不改磁盘结果，只重算过滤后的派生展示 |
| 两类摘要均未变化 | 直接加载已有结果，不创建运行任务 |

PRED 必须保留重算评估所需的完整 score 和几何。不能在 Python 侧按当前 UI confidence 阈值丢弃预测；如果框架必须设置最低导出分数，该最低分数属于推理参数，用户把评估阈值调到更低时必须提示需要重新推理。

#### 完整推理流程

1. 校验将要清理的绝对路径严格等于当前任务的 `pred/`、`evaluation/` 和 `result.yaml`；
2. 删除旧 `pred/`、`evaluation/`、`result.yaml` 后重新创建目录；
3. 导出数据并写入 `pred/images.txt` 与 `pred/config.yaml`；
4. Python 只写当前 `pred/`，完成后由 C++ 使用 `yaml-cpp` 校验图像清单、预测清单、路径和 schema，并原子规范化重写 `manifest.yaml`；
5. C++ 读取 PRED 和 GT，生成包含评估配置、图像记录和实例事件的 `evaluation/report.yaml`；
6. 校验评估结果后，最后原子写入 `result.yaml`；
7. 只有第 6 步成功，任务才进入 `Finished` 和 100%。

推理或评估失败时不恢复旧结果，也不能把半成品显示为有效结果；页面使用 `TaskManager` 的失败信息展示错误。用户再次运行时仍从清理目标目录开始。

#### 仅重新评估流程

1. 校验 `pred/config.yaml`、`pred/images.txt` 和 `pred/manifest.yaml` 完整有效；
2. 保留 `pred/`，只删除旧 `evaluation/` 和 `result.yaml`；
3. C++ 使用当前评估参数和当前 GT 重新生成 `evaluation/report.yaml`；
4. 最后原子写入 `result.yaml` 并刷新页面。

停止任务必须同时支持终止 Python 和取消 C++ 评估。Python 正常退出只表示推理阶段结束，不能直接调用 `finishTask()`。

### 7.4 并发规则

- 同一测试任务最多运行一个实例；
- 不同测试任务因目录完全隔离，可以支持并行测试；
- 第一阶段建议禁止同一模型训练和测试同时运行，避免测试读取正在写入的权重；
- 未来如需并行，应在 `pred/config.yaml` 中记录所用 checkpoint 的相对路径、大小、mtime 和内容摘要，并确保运行期间该文件不被覆盖。

## 8. 测试配置和结果协议

### 8.1 可编辑 config.yaml

```yaml
schema_version: 2

model_uuid: "..."
model_name: "模型A"
task_type: test

test_task:
  uuid: "..."
  name: "测试任务A"

paths:
  weight_dir: "../../train/weights"
  prediction_dir: "pred"
  evaluation_dir: "evaluation"

dataset_selection: {}
datasets: {}
test_params: {}

evaluation:
  confidence_threshold: 0.5
  iou_threshold: 0.5
  image_threshold: 0.5
  matching_strategy: hungarian_iou
```

`config.yaml` 只保存用户可编辑的任务定义和相对路径，不作为某次结果的实际配置。启动 Python 时，配置服务根据它生成仅供当前进程消费的绝对路径参数；持久文件仍使用相对任务根目录路径。

推理相关字段和评估相关字段必须由参数 schema 明确分类，不能靠字段名猜测。`pred/config.yaml` 保存规范化后的实际推理配置、`inference_digest`、checkpoint 相对路径及 hash/mtime；`evaluation/report.yaml` 保存实际评估配置、`evaluation_digest`、所用 GT/类别版本和引用的 `inference_digest`。

摘要组成建议：

```text
inference_digest = hash(
    method + checkpoint_digest + dataset_selection +
    images.txt 内容 + input_data_digest + preprocessing +
    inference_params + prediction_decoder_class_mapping
)

evaluation_digest = hash(
    inference_digest + evaluation_params + gt_revision +
    display_class_catalog + metric_adapter_version
)
```

`input_data_digest` 至少覆盖图像 ID、规范化路径、文件大小和 mtime，避免图像内容在同一路径被替换后仍误判为可复用。checkpoint 同时记录相对路径、大小、mtime 和内容 hash；快速比较可先用大小/mtime，准备复用 PRED 前仍以内容摘要或可信模型版本校验。预测解码所用的 class ID 映射属于推理摘要，类别显示名称和颜色属于评估摘要。

### 8.2 result.yaml

```yaml
schema_version: 1
model_uuid: "..."
test_task_uuid: "..."
status: finished
inference_digest: "sha256:..."
evaluation_digest: "sha256:..."
weight_digest: "sha256:..."
inference_completed_at: 1785480050
evaluated_at: 1785480100
image_count: 50
prediction_count: 50
prediction_dir: "pred"
prediction_images: "pred/images.txt"
prediction_manifest: "pred/manifest.yaml"
evaluation_report: "evaluation/report.yaml"
```

`result.yaml` 只保存列表和页面快速加载所需的摘要。大量实例和曲线点不放入该文件。

### 8.3 预测输出语义

普通模型的推理入口最终必须直接写入 `prediction_dir`。

当前 anomalib 入口会自行追加 `/pred`，需要在普通模型推理适配层统一为直接消费 `prediction_dir`，避免生成 `pred/pred`。QML 和通用配置服务不应了解框架路径差异。

`pred/images.txt` 示例：

```text
image_id,image_path
101,"datasets/test/a,01.png"
102,datasets/test/b.png
```

`image_path` 与项目数据库记录一致，并按标准 CSV 引号规则转义逗号和双引号。C++ 只能通过 CSV 解析器读取，不能用简单字符串 `split(',')`。清单必须包含全部实际推理图像，即使某张图没有任何预测；`manifest.yaml` 则保存全部预测实例及原始 score、类别、几何和预测文件引用。

`manifest.yaml` 基本结构：

```yaml
schema_version: 1
model_uuid: "..."
test_task_uuid: "..."
method: object_detection
record_count: 1
records:
  - prediction_id: pred-001
    image_id: 101
    class_id: 1
    score: 0.96
    geometry:
      type: bbox
      coordinate_system: image_pixels
      format: xywh
      values: [120.0, 80.0, 300.0, 240.0]
    artifact_path: ""
```

`prediction_id` 在当前 PRED 内唯一；`image_id` 必须存在于 `images.txt`；score 必须为有限数值；几何格式遵循第 5.4 节统一协议。分类或图像级异常检测可以省略局部几何，分割预测可以通过任务根目录相对 `artifact_path` 引用 mask 文件。

### 8.4 YAML 读写基础设施

项目已经存在 `src/common/include/common/YamlUtils.h` 和 `src/common/YamlUtils.cpp`，并已在 `dltool_common` 中链接 `yaml-cpp`。本方案不在 model 模块重复封装 YAML，直接复用：

```cpp
common::yaml::loadFile(...);
common::yaml::writeFile(...);
common::yaml::nodeString(...);
common::yaml::scalarNode(...);
common::yaml::setMapValue(...);
common::yaml::variantToYaml(...);
common::yaml::nodeVariant(...);
```

现有 `writeFile()` 使用 `QFile + Truncate`，不具备原子提交语义。建议只在 common 增加通用接口：

```cpp
namespace dltool::common::yaml {

COMMON_API bool writeFileAtomic(
    const QString &path,
    const YAML::Node &node,
    QString *err_msg = nullptr);

} // namespace dltool::common::yaml
```

实现使用 `YAML::Emitter` 生成 UTF-8 内容，再通过 `QSaveFile::write()` 和 `commit()` 提交。C++ 生成的 `tasks.yaml`、所有 `config.yaml`、`report.yaml` 和最后的 `result.yaml` 均使用该接口。Python 完成 `manifest.yaml` 后，C++ 使用 `yaml-cpp` 完整解析、校验并通过 `writeFileAtomic()` 规范化重写，之后才允许进入评估阶段。`result.yaml` 仍最后写入，作为整套结果有效的提交标志。现有 `writeFile()` 保留给不要求原子替换的旧调用者，是否整体切换到 `QSaveFile` 另行评估，避免暗中改变公共接口语义。

协议文件应使用类型化解析器直接检查 `YAML::Node` 的 Map/Sequence/Scalar 类型和必填字段，并捕获 `YAML::Exception`、文件异常及类型转换异常。`nodeVariant()`/`variantToYaml()` 适合开放式参数和 chart descriptor；任务 UUID、image ID、score、几何等稳定协议字段应显式 `as<T>()` 解析，避免通用 QVariant 标量推断改变字符串 ID 类型。

`manifest.yaml` 顶层统一为：

```yaml
schema_version: 1
record_count: 2
records:
  - {}
  - {}
```

写入前校验 `record_count` 与 sequence 长度，读取后校验 schema 版本、必填字段、有限数值、重复 ID 和路径边界。Python 可使用标准 YAML 库生成同一协议，C++ 的最终校验和后续读写仍以 `yaml-cpp` 为准。

## 9. 评估匹配规则

### 9.1 当前工作点

混淆矩阵、实例指标和实例网格表示一个固定评估工作点：

- 固定置信度阈值；
- 固定 IoU 或方法对应的匹配阈值；
- 固定 `pred/images.txt` 界定的实际图像集合；
- 固定 `pred/config.yaml` 记录的 checkpoint 和推理配置。

PR 曲线等图表可以跨置信度阈值计算，因此其数值不要求与某一个矩阵单元完全对应，但图表中应标记当前工作点。

### 9.2 实例匹配

目标检测和实例分割建议采用以下默认流程：

1. 按图像分别处理；
2. 去除低于置信度阈值的 PRED；
3. 计算所有 GT/PRED 候选对的 IoU；
4. 使用一对一最大 IoU 匹配，优先使用 Hungarian；
5. 低于 IoU 阈值的候选对不匹配；
6. 几何匹配后再判断类别是否一致；
7. 同类别匹配记为 TP；
8. 异类别匹配记入混淆矩阵非对角线；
9. 未匹配 PRED 记为 FP；
10. 未匹配 GT 记为 FN。

采用类别无关的几何匹配，是为了让“几何位置正确但类别预测错误”能够进入混淆矩阵非对角线。如果方法的官方评估规则不同，由评估适配器同时输出官方指标和用于详情分析的匹配结果。

### 9.3 指标公式

设矩阵行表示 PRED 类别，列表示 GT 类别，`M[p][g]` 表示匹配数量。

对类别 `c`：

```text
TP_c = M[c][c]
FP_c = sum(M[c][g], g != c) + unmatched_pred_c
FN_c = sum(M[p][c], p != c) + unmatched_gt_c

Precision_c = TP_c / (TP_c + FP_c)
Recall_c    = TP_c / (TP_c + FN_c)
F1_c        = 2 * Precision_c * Recall_c / (Precision_c + Recall_c)
```

上述公式定义的是 `diagnostic_metrics`。顶部“整体”显示报告 `primary_metric_set` 指定的指标；目标检测等方法优先显示 adapter 输出的官方工作点 P/R/F1，官方指标不可用时才回退到 diagnostic micro-average，并明确标记“诊断口径”。按类别弹窗同时显示：

- 类别名称；
- GT 数量；
- PRED 数量；
- TP、FP、FN；
- Precision、Recall、F1；
- 可选 macro-average 汇总。

分母为零时显示 `—`，不能把未定义指标伪装为 `0.000`。

### 9.4 图像级指标

图像级 Precision、Recall、F1 不强制所有方法使用同一种样本定义，由方法适配器声明：

```yaml
image_metric_definition:
  sample_unit: image_class_presence
  aggregation: micro
  positive_definition: gt_or_pred_class_present
```

异常检测可以使用每图 normal/anomaly，分类可以使用每图分类，检测或分割可以选择 `(image, class)` presence 或方法自身定义。报告必须写入样本单位、正例定义、聚合方式和阈值，指标面板的 tooltip 原样显示；没有合理图像级定义的方法返回 `has_image_metrics=false`，不能硬套 presence 公式。

### 9.5 评估结果应用全局过滤条件

测试评估必须支持数据页已经存在的过滤栏。过滤条件来源仍为 `DataManager.globalFilter`，包括：

- 数据集（`Dataset`）；
- 图像/实例标签类别（`LabelClass`、`ImageLabelClass`）；
- Tag（`Tag`）；
- 文件名文本；
- 已有的自定义搜索结果条件（`Custom`）。

过滤通过 `QSortFilterProxyModel` 完成，不额外复制一套过滤状态。报告和 SourceModel 始终代表完整测试结果；ProxyModel 代表当前过滤栏下的派生视图。过滤不修改测试任务配置、原始报告或 PRED。

建议使用两条 Source/Proxy 链：

```text
EvaluationImageSourceModel
    -> EvaluationImageFilterProxyModel
        -> 图像指标和图像级图表

EvaluationInstanceSourceModel
    -> EvaluationGlobalFilterProxyModel
        -> 实例指标、按类别指标、混淆矩阵、实例级图表
        -> EvaluationCellFilterProxyModel
            -> 实例 GridView
```

分成 Image 和 Instance 两个 SourceModel，是因为图像级统计必须包含没有任何实例事件的图像，例如异常检测中的正常图像或检测任务中的 true negative。`EvaluationImageSourceModel` 以 `pred/images.txt` 为全集，再由 C++ 查询当前 GT、数据集和 Tag 元数据，不能只根据实例列表反推全部图像。

`EvaluationGlobalFilterProxyModel` 和 `EvaluationImageFilterProxyModel` 持有 `QPointer<GlobalFilter>`。它们只监听汇总信号：

```cpp
GlobalFilter::filterChanged
```

`GlobalFilter` 一次操作可能连续发出多个细粒度信号，同时监听会造成重复刷新。收到 `filterChanged` 后调用 `invalidateFilter()` 或 Qt 版本对应的行过滤失效接口，由 `filterAcceptsRow()` 在 C++ 中重新判断。ProxyModel 不向 QML 暴露过滤计算细节。

`EvaluationCellFilterProxyModel` 只处理评估页面内部条件：

- 当前混淆矩阵单元格；
- 可选 PRED 类别；
- 可选状态类型；
- 可选分数范围。

它的 sourceModel 是 `EvaluationGlobalFilterProxyModel`。因此点击混淆矩阵只缩小 GridView，不会反过来改变顶部指标、图表或混淆矩阵本身。

#### 过滤作用域

过滤条件按评估事件的图像、GT 和 PRED 三个层级解释：

| 全局过滤条件 | 评估事件的判断对象 |
|---|---|
| 数据集 | `event.image_id` 对应图像所属数据集 |
| Tag | 图像 Tag 或该图像 GT 标注的 Tag，遵循 `GlobalFilter::acceptsImage/acceptsLabel` 语义 |
| 文件名 | `event.image_path` 或图像名称 |
| ImageLabelClass | 图像级类别，作用于事件所属图像 |
| LabelClass | 默认作用于 GT 类别；若事件只有 PRED，则作用于 PRED 类别以便查看纯 FP |
| Label 搜索结果 | GT label ID 命中时保留；纯 FP 没有 GT 时不因该条件伪造命中 |
| Image 搜索结果 | 事件图像命中时保留 |
| 其他 Custom 条件 | 复用 `GlobalFilter` 对图像/标签的已有判断 |

默认策略是“事件级通过”：只有事件满足所有已启用的过滤维度时，`EvaluationGlobalFilterProxyModel::filterAcceptsRow()` 才返回 true。

判断规则：

- 有 GT label ID 的事件调用 `GlobalFilter::acceptsLabel(gt_label_id)`；
- 纯 FP 没有 GT，先调用 `GlobalFilter::acceptsImage(image_id)`，再使用 `GlobalFilter::acceptsLabelClassId(pred_class_id)` 应用标签类别条件；
- 只有 Label 搜索结果条件时，纯 FP 因没有 GT label ID 而不命中；
- 对同时存在 GT/PRED 的事件，全局 LabelClass 按 GT 类别解释；
- PRED 类别筛选由下一级 `EvaluationCellFilterProxyModel` 处理，不改变全局过滤栏。

`EvaluationImageFilterProxyModel` 不能只调用 `GlobalFilter::acceptsImage()`，因为现有 image 查询不会应用实例 `LabelClass`。图像行按以下规则补充判断：

- LabelClass 正向选择：图像至少包含一个属于所选类别的 GT；
- LabelClass 反选：图像不得包含任何被排除类别的 GT；
- LabelClass 为空或未启用：不增加类别约束，包含无 GT 的图像；
- Label 搜索结果：图像至少包含一个命中的 GT label；
- 纯 FP 的 PRED 类别只用于实例链，不把它伪装成图像的 GT 类别；
- `ImageLabelClass`、Dataset、Tag、文件名、Image 搜索结果继续遵循 `acceptsImage()`。

为避免 ProxyModel 复制 `GlobalFilter` 的反选和空选择规则，建议在 `GlobalFilter` 增加一个小型只读查询：

```cpp
bool acceptsLabelClassId(qint64 label_class_id) const;
```

该接口内部直接复用现有 `passesIdFilter()`，不引入新的过滤状态对象。

全局过滤的候选集合还要与当前测试任务实际运行的数据集范围取交集。过滤栏选中了测试任务之外的数据集或标签时，结果应为空，而不是读取其他数据集的评估结果。

#### 对指标和矩阵的影响

过滤后的数据必须重新计算以下所有派生结果：

1. 实例整体 Precision、Recall、F1、TP、FP、FN；
2. 按类别指标；
3. 图像级 Precision、Recall、F1；
4. 混淆矩阵的类别行列、FP 列、FN 行和合计行列；
5. 当前图表 descriptor 中依赖样本集合的曲线或分布；
6. GridView 的事件数量和当前选中项。

`ModelEvaluationViewModel` 监听两个全局过滤 ProxyModel 的 `modelReset`、`rowsInserted`、`rowsRemoved`、`layoutChanged` 和必要的 `dataChanged`，合并连续变更后在 C++ 重新聚合指标、矩阵和图表。

ProxyModel 和后台线程的边界必须固定：

1. 两个 ProxyModel 始终只在 GUI 线程过滤；
2. GUI 线程遍历可见行，只收集稳定的 `event_uuid`、`image_id` 或底层记录序号；
3. 工作线程只接收这些 ID/序号，并读取生命周期受控、加载后不再修改的纯 C++ 记录；
4. 工作线程返回纯值聚合结果；
5. GUI 线程核对 revision 后更新 Qt Model。

工作线程不得遍历 `QSortFilterProxyModel`，不得持有 `QModelIndex`，也不得读取任何 QML 状态。本设计不新增一组 `Snapshot` 类；线程输入只是稳定 ID/序号和只读记录存储。

#### YAML 加载和 Model 虚拟化

过滤和指标必须覆盖完整结果，因此不能只读取 GridView 首屏。`yaml-cpp` 和现有 `common::yaml::loadFile()` 都会完整解析一个 YAML 文档，不能设计基于文件行偏移的伪懒加载。报告的 `instance_records` 在工作线程完整解析后，转换成类型明确的 C++ 详情记录：

```cpp
struct EvaluationEventIndex
{
    QString event_uuid;
    qsizetype detail_index{-1};
    qint64 image_id{-1};
    qint64 dataset_id{-1};
    qint64 gt_label_id{-1};
    qint64 gt_class_id{-1};
    qint64 pred_class_id{-1};
    EvaluationStatus status;
    double score{0.0};
};
```

SourceModel 的每一行对应一个索引项，ProxyModel 在这些轻量字段及图像元数据上完成全量过滤。几何、polygon、mask 引用和详情文本保存在同一个只读 C++ record store 中，只有可见 delegate 或详情面板请求角色时才转换为 `QVariant`；QML 的 GridView 仍只实例化可见 delegate。`manifest.yaml` 同样完整解析并建立包含 image ID、class ID、score 和详情序号的轻量索引，供 PR/ROC 和重新评估读取。

解析完成后立即释放 `YAML::Node` 树，避免 YAML 节点树和 C++ 记录长期占用双份内存。加载前检查文件大小、schema 和 records 数量上限；若真实规模测试证明单文件不可接受，再把同一 YAML schema 扩展为 `chunks` 引用的多个 `.yaml` 文件，但不新增 QML 数据模型。

过滤只改变派生视图，不重新运行推理或改写磁盘评估。实例指标和矩阵基于报告中的工作点事件重新聚合；分数分布、PR/ROC 和官方指标不能从固定阈值后的事件重建，必须从完整 `pred/manifest.yaml`、`pred/images.txt` 和 GT 在 C++ 重新计算。图表 Calculator 使用 Image Proxy 产生的可见 image ID 集合以及 `GlobalFilter` 的类别查询过滤完整预测，不能只缩放全量报告中的聚合曲线。

过滤后类别集合是否收缩必须明确：默认保留完整报告的类别轴，只把被过滤后无数据的单元格显示为 0，这样不同过滤条件之间矩阵行列不会跳动；同时提供 `compactClassAxis` 能力供后续需要时启用。

指标标题或 tooltip 显示：

```text
当前过滤：数据集=缺陷测试集，标签=缺陷/划痕，Tag=批次A
样本数：图像 120，评估事件 436
```

没有启用过滤时显示“全部测试样本”。

#### 过滤应用时序

```text
GlobalFilter 变化
    ↓
EvaluationImageFilterProxyModel / EvaluationGlobalFilterProxyModel
    ↓
QSortFilterProxyModel 重新过滤 SourceModel
    ↓
ModelEvaluationViewModel 合并 ProxyModel 变更通知
    ↓
EvaluationMetricsCalculator / EvaluationConfusionMatrixBuilder /
EvaluationChartCalculator 在 C++ 重算
    ↓
指标、矩阵和图表 Model 更新
    ↓
QML 只收到 Model/Property 变化并展示
```

ViewModel 维护递增的 `aggregationRevision`。如果复杂聚合放到 C++ 工作线程，提交结果时必须核对测试任务 UUID、报告 revision 和 aggregation revision，避免快速切换任务或过滤条件造成旧结果覆盖新结果。revision 只用于识别过期计算，不保存过滤条件。

过滤条件不写入 `report.yaml`。页面重新打开时直接使用当前 `GlobalFilter` 状态，不在测试任务内另存一套重复过滤状态。

## 10. TestEvaluationPanel 总体布局

最外层使用垂直 `QuiSplitView`，分为上下两部分：

```text
┌────────────────────────────────────────────────────────────┐
│ 实例统计 │ 图像统计 │ 方法特有图表                         │
├────────────────────────────────────────────────────────────┤
│ 混淆矩阵 │ 实例图像 GridView │ 实例详情                    │
└────────────────────────────────────────────────────────────┘
```

`TestEvaluationPanel` 内部建议尺寸：

- 上半部分默认约占 36%，使用 preferredHeight，不设置 240 的硬最小高度；
- 下半部分填充剩余空间，使用 preferredHeight，不设置 360 的硬最小高度；
- 上部左、中各约 28%，右部填充剩余空间；
- 下部混淆矩阵约 34%，实例网格约 42%，详情约 24%；
- 各区域设置合理最小宽度，分隔条允许用户调整并保留页面生命周期内的尺寸。

现有 `TestPanel.qml` 的设置区约占页面一半，如果再叠加上述硬最小高度，在常见窗口中会溢出。因此 `TestPanel` 外层也改为垂直 `QuiSplitView`：上部为可折叠的任务/参数/数据集设置，下部为评估面板。小窗口时允许折叠设置区；各子面板内容超出后使用局部滚动，不让整个评估面板被裁掉。

外层不承担任何指标计算，只绑定：

```qml
property ModelEvaluationViewModel evaluation: null
```

## 11. 上半部分设计

### 11.1 左侧：实例统计

显示：

- 整体 Precision；
- 整体 Recall；
- 整体 F1；
- TP、FP、FN 小型摘要；
- 当前 confidence、IoU 阈值；
- “按类别查看”按钮。

三个主指标使用等宽指标卡，显示百分比和原始小数 tooltip。指标不可用时显示 `—`。

标题栏同时显示“官方口径”或“诊断口径”。tooltip 至少说明 `primary_metric_set`、样本单位、聚合方式、confidence/IoU 阈值；混淆矩阵和实例列表始终属于诊断匹配口径，即使顶部显示官方指标也不能暗示二者完全等价。

“按类别查看”打开 `ClassMetricsDialog.qml`。弹窗使用表格，默认按类别顺序排列，支持按 F1、FP、FN 排序，不把类别明细直接堆入主面板。

### 11.2 中间：图像统计

显示：

- 图像级 Precision；
- 图像级 Recall；
- 图像级 F1；
- 图像级 TP、FP、FN；
- 当前统计口径说明。

实例统计和图像统计复用同一个 `EvaluationMetricsPanel.qml`，通过 `title`、`metricsModel` 和 `showClassDetails` 配置，不复制三套指标卡代码。

### 11.3 右侧：方法特有图表

使用已经通用化的 `QuiChart`，图表由 `EvaluationChartDescriptor` 描述：

```cpp
struct EvaluationChartDescriptor
{
    QString id;
    QString title;
    QString kind;
    QString chart_type;
    QVariantMap chart_data;
    QVariantMap chart_options;
};
```

示例：

| 方法 | 默认图表 |
|---|---|
| 异常检测 | normal/anomaly 异常分数分布直方图 |
| 目标检测 | 总体或按类别 PR 曲线 |
| 分类 | ROC、PR 或置信度分布 |
| 分割 | IoU/Dice 分布或阈值曲线 |

如果报告提供多个图表，标题栏显示图表下拉列表；只有一个时隐藏下拉列表。QML 不根据方法选择图表，只显示 ViewModel 提供的 descriptors。

## 12. 下半部分设计

### 12.1 左侧：混淆矩阵

混淆矩阵使用独立 `EvaluationConfusionMatrixModel : QAbstractTableModel`，不能在 QML 中临时计算。

设类别数量为 `C`，最终矩阵为 `(C + 2) × (C + 2)`：

- 列：GT 类别、FP、合计；
- 行：PRED 类别、FN、合计。

结构示例：

| PRED \ GT | 类别 A | 类别 B | FP | 合计 |
|---|---:|---:|---:|---:|
| 类别 A | TP/匹配 | 类别错误 | 未匹配 PRED A | PRED A 总数 |
| 类别 B | 类别错误 | TP/匹配 | 未匹配 PRED B | PRED B 总数 |
| FN | 未匹配 GT A | 未匹配 GT B | — | FN 总数 |
| 合计 | GT A 总数 | GT B 总数 | FP 总数 | 评估事件总数 |

最后一个右下角单元格定义为：

```text
匹配对数量 + 未匹配 PRED 数量 + 未匹配 GT 数量
```

它表示评估事件总数，而不是强行要求 GT 总数和 PRED 总数相等。

矩阵 Model roles：

```cpp
CountRole
CellKindRole
PredClassIdRole
GtClassIdRole
IsDiagonalRole
IsErrorRole
SelectableRole
TooltipRole
```

视觉规则：

- 对角线匹配单元格使用绿色强度；
- 非对角线类别错误使用橙色或红色强度；
- FP、FN 使用错误色；
- 合计行列使用中性色；
- 当前选中单元格使用高亮边框；
- 颜色强度使用当前矩阵最大值归一化，但始终显示真实实例数量。

### 12.2 矩阵单元格与 GridView 联动

点击单元格后生成结构化过滤条件，而不是在 QML 中拼字符串：

```cpp
enum class EvaluationCellKind
{
    Match,
    FalsePositive,
    FalseNegative,
    PredTotal,
    GtTotal,
    All,
    NotApplicable,
};

struct EvaluationCellFilter
{
    EvaluationCellKind kind;
    qint64 pred_class_id{-1};
    qint64 gt_class_id{-1};
};
```

过滤语义：

| 点击位置 | GridView 内容 |
|---|---|
| 类别行 × 类别列 | 只显示该 PRED/GT 类别组合的匹配事件 |
| 类别行 × FP 列 | 只显示该 PRED 类别的未匹配预测 |
| FN 行 × 类别列 | 只显示该 GT 类别的未匹配真值 |
| 类别行 × 合计列 | 显示该 PRED 类别的全部事件 |
| 合计行 × 类别列 | 显示该 GT 类别的全部事件 |
| 右下角 | 显示全部事件 |

切换测试任务时默认选择右下角“全部”；重新加载同一报告时，如果原过滤条件仍有效则保留。

### 12.3 中间：实例图像 GridView

新增 `EvaluationInstancesGridView.qml`，交互可参考 `LabelInstancesGridView.qml`：

- GridView 虚拟化；
- 滚轮滚动；
- Ctrl + 滚轮缩放；
- 键盘方向移动；
- 单选当前缩略图；
- 空状态提示；
- 自动滚动到当前项。

但不得直接复用 `DataManager.labelInstances`，而应绑定：

```qml
property QAbstractItemModel instancesModel
property ItemSelectionModel selection
```

实例 Model roles 建议包含：

```cpp
EventUuidRole
ImageIdRole
ImageNameRole
ImagePathRole
StatusRole
StatusTextRole
GtInstanceIdRole
GtClassIdRole
GtClassNameRole
GtClassColorRole
GtGeometryRole
PredInstanceIdRole
PredClassIdRole
PredClassNameRole
PredClassColorRole
PredGeometryRole
PredScoreRole
IouRole
CropBoundsRole
ThumbnailUrlRole
SelectedRole
```

过滤在 C++ Model 中执行。矩阵点击后调用 `setCellFilter()`，Model 更新可见索引，而不是重新读取报告文件。

### 12.4 评估缩略图

新增 `EvaluationInstanceThumbnail.qml`，不能直接使用现有只支持一个 label ID 的 `LabelInstanceThumbnail.qml`。

裁剪范围：

- 同时有 GT 和 PRED：使用二者矩形包围盒的并集；
- 只有 GT：使用 GT bounds；
- 只有 PRED：使用 PRED bounds；
- 分类或没有局部几何：显示完整图像；
- 在并集之外增加固定像素和比例 padding；
- 裁剪范围必须限制在原图边界内。

覆盖层：

- GT 使用实线；
- PRED 使用虚线；
- GT/PRED 使用各自类别颜色；
- 同类别时仍通过实线和虚线区分；
- 检测绘制矩形；
- 分割绘制多边形轮廓或 mask 轮廓；
- FP 只绘制 PRED；
- FN 只绘制 GT；
- 缩略图角标显示 `TP`、`类别错误`、`FP` 或 `FN`。

Canvas 绘制虚线使用 `setLineDash()`。所有几何映射基于 `crop_bounds`，不能分别按 GT 或 PRED 自己的 bounds 缩放，否则二者会错位。

已实现 `EvaluationThumbnailImageProvider`，由应用的 `QQmlApplicationEngine` 注册：

```text
image://evaluationthumbnail/<event_uuid>?event=<event_uuid>&revision=<result_revision>&path=<encoded_path>&x=<x>&y=<y>&width=<width>&height=<height>
```

`result_revision` 由当前 `inference_digest`、`evaluation_digest` 或结果文件修订号生成，用于避免重跑或重新评估后命中旧缩略图缓存，不需要持久运行 ID。Provider 只返回裁剪后的底图；GT/PRED 覆盖层由 QML 绘制。评估事件 Model 应提前给出裁剪框和归一化几何，避免 QML 访问数据库或自行读取图片尺寸。

### 12.5 右侧：实例详情

新增 `EvaluationInstanceDetailsPanel.qml`，绑定 GridView 当前项。

基础字段：

- 所属图像名称；
- 图像 ID 或路径；
- GT 标签类别；
- PRED 标签类别；
- 预测置信度；
- IoU 或方法对应匹配值；
- 预测状态；
- 当前 confidence/IoU 阈值；
- GT/PRED 实例 ID；
- GT/PRED 几何摘要。

状态显示：

| 状态 | 显示文本 |
|---|---|
| `true_positive` | 正确匹配 |
| `class_mismatch` | 匹配成功，类别错误 |
| `false_positive` | 错误预测，未匹配 GT |
| `false_negative` | 漏检，GT 未匹配 PRED |

缺少 GT 或 PRED 时显示 `—`，不能显示虚构的类别或零值。

## 13. 评估 ViewModel

建议新增聚合对象：

```cpp
class ModelEvaluationViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state ...)
    Q_PROPERTY(QString errorString ...)
    Q_PROPERTY(EvaluationMetricsModel *instanceMetrics ...)
    Q_PROPERTY(EvaluationClassMetricsModel *classMetrics ...)
    Q_PROPERTY(EvaluationMetricsModel *imageMetrics ...)
    Q_PROPERTY(EvaluationChartListModel *charts ...)
    Q_PROPERTY(EvaluationConfusionMatrixModel *confusionMatrix ...)
    Q_PROPERTY(EvaluationCellFilterProxyModel *instances ...)
    Q_PROPERTY(QItemSelectionModel *instanceSelection ...)
    Q_PROPERTY(QVariantMap selectedInstance ...)
    Q_PROPERTY(bool globalFilterActive ...)
    Q_PROPERTY(QString globalFilterDescription ...)
    Q_PROPERTY(QString metricScopeDescription ...)
    Q_PROPERTY(QString resultRevision ...)
    Q_PROPERTY(bool inferenceOutdated ...)
    Q_PROPERTY(bool evaluationOutdated ...)
};
```

建议增加以下后端接口，确保过滤和计算不落到 QML：

```cpp
Q_INVOKABLE void refresh();
Q_INVOKABLE void setPredClassFilter(qint64 class_id);
Q_INVOKABLE void clearPredClassFilter();
Q_INVOKABLE bool selectConfusionCell(int row, int column);
Q_INVOKABLE void clearConfusionCellFilter();
Q_INVOKABLE bool selectInstance(const QString &event_uuid);
```

这些接口的实现位置是 C++，并应拆分为以下服务或计算器：

```text
EvaluationReportRepository       读取 result/report/instances 和 PRED 清单
EvaluationImageSourceModel       以 images.txt 构造完整图像级记录
EvaluationInstanceSourceModel    保存完整实例评估事件
EvaluationImageFilterProxyModel  应用 GlobalFilter 的图像级条件
EvaluationGlobalFilterProxyModel 应用 GlobalFilter 的实例级条件
EvaluationCellFilterProxyModel   应用矩阵单元格和 PRED 条件
EvaluationMetricsCalculator      重新计算实例/图像指标
EvaluationConfusionMatrixBuilder 生成 C+2 阶矩阵和 CellFilter
EvaluationChartCalculator        对过滤后的样本重建分布/曲线
EvaluationInstanceIndex          建立事件到文件行/图像的索引
EvaluationPredictionIndex        建立完整预测清单的轻量索引
```

`EvaluationCellFilterProxyModel` 的 `rowCount()`、过滤后的索引、详情 QVariant、矩阵单元数量和图表数据都由 C++ 提供。QML 不得执行以下逻辑：

- IoU、匹配或 TP/FP/FN 计算；
- Precision、Recall、F1 或曲线点计算；
- 按数据集、Tag、标签类别遍历实例；
- 从原始报告重建混淆矩阵；
- 根据矩阵单元拼接事件过滤表达式；
- 计算 GT/PRED bounds 并集或坐标归一化；
- 读取文件、解析 YAML 或访问数据库。

QML 允许的逻辑仅限于：

- 绑定 `Q_PROPERTY` 和 Model roles；
- 转发按钮、下拉框、单元格和缩略图点击信号；
- 控制 `SplitView`、`GridView` 和弹窗布局；
- 显示后端已计算好的数字、颜色、状态文本和 tooltip；
- 调用 `QuiChart` 渲染后端提供的 chart descriptor。

状态至少包括：

```text
NotRun
Loading
Ready
Running
Failed
MissingReport
InvalidReport
```

加载策略：

1. 先读取 `result.yaml`；
2. 校验 `result.yaml` 引用的 `pred/config.yaml`、`pred/images.txt`、`pred/manifest.yaml`、`report.yaml` 和报告中的 `dataset_manifest`；
3. 比较当前可编辑配置与推理/评估摘要，设置 `inferenceOutdated`、`evaluationOutdated`；
4. 异步读取并校验 `report.yaml`；
5. 加载指标、矩阵和图表；
6. 完整解析报告内的 `instance_records` 和 `image_records`，Qt Model 作为运行期唯一内存数据源；
7. 切换任务时取消旧加载和聚合请求；
8. 使用 task UUID、result revision 和 aggregation revision 防止异步结果回写到错误任务。

页面状态提示：

- `inferenceOutdated=true`：显示“推理输入已变化，需要重新推理”，旧结果可以只读查看但标记过期；
- 仅 `evaluationOutdated=true`：显示“评估参数或 GT 已变化，可复用当前预测重新评估”；
- 两者均为 false：当前结果与配置一致；
- 没有 `result.yaml`：即使目录中存在半成品也不加载为有效结果。

## 14. QML 组件拆分

建议将 `TestEvaluationPanel.qml` 保持为编排容器，并拆分以下组件：

```text
src/model/qml/test/evaluation/
├─ EvaluationMetricsPanel.qml
├─ MetricCard.qml
├─ ClassMetricsDialog.qml
├─ EvaluationChartPanel.qml
├─ ConfusionMatrixPanel.qml
├─ EvaluationInstancesGridView.qml
├─ EvaluationInstanceThumbnail.qml
├─ EvaluationInstanceDetailsPanel.qml
├─ EvaluationEmptyState.qml
└─ EvaluationErrorState.qml
```

职责约束：

- `TestEvaluationPanel.qml`：SplitView 布局和整体状态切换；
- `EvaluationMetricsPanel.qml`：展示给定维度的指标；
- `EvaluationChartPanel.qml`：消费通用 chart descriptor；
- `ConfusionMatrixPanel.qml`：表格、表头、选择和 tooltip；
- `EvaluationInstancesGridView.qml`：实例列表和选择；
- `EvaluationInstanceThumbnail.qml`：裁剪图上的双几何覆盖层；
- `EvaluationInstanceDetailsPanel.qml`：当前事件字段展示。

## 15. 算法扩展接口

不同方法通过 C++ 评估适配器生成同一个标准报告。建议定义能力描述：

```cpp
struct EvaluationCapabilities
{
    bool has_instance_metrics{false};
    bool has_image_metrics{false};
    bool has_confusion_matrix{false};
    bool has_instance_events{false};
    QStringList chart_kinds;
};
```

建议的数据生成层次：

1. C++ 任务准备阶段写入 `pred/images.txt` 和 `pred/config.yaml`；
2. 普通模型预测脚本消费图像清单并写入完整、规范化的 `pred/manifest.yaml`；
3. C++ `ModelEvaluationService` 在后台读取当前 PRED 和对应图像的 GT；
4. 注册到 C++ Registry 的方法评估适配器执行规范化和方法特有处理；
5. 通用 C++ Calculator 执行匹配、指标、矩阵和曲线计算；
6. C++ 输出统一 `report.yaml`，实例事件嵌入 `instance_records`；
7. C++ ViewModel 验证 schema 并暴露 Qt Model；
8. QML 按 capabilities 显示或隐藏区域。

方法特有扩展只允许发生在：

- 预测结果到规范化预测记录的转换；
- 实例匹配策略；
- 图像级指标定义；
- 图表 descriptor 生成。

不得把算法名称判断散落到各个 QML 面板。

Python 端不负责为 QML 拼装 chartData、矩阵或详情字段，只需保证预测清单包含 C++ 评估所需的图像 ID、类别、完整 score、几何和预测文件引用。普通模型测试始终只有一个测试数据集选择；小样本学习不接入此 adapter registry。

## 16. 状态同步

实时进度由 `TaskManager` 作为唯一事实来源。

模型 `extra_data` 只保存轻量恢复信息：

```yaml
train: {}
test_tasks:
  "<测试任务UUID>":
    name: 测试任务A
    status: finished
    progress: 100
    prediction_dir: test/测试任务A/pred
    evaluation_report: test/测试任务A/evaluation/report.yaml
    inference_digest: "sha256:..."
    evaluation_digest: "sha256:..."
```

`extra_data.test_tasks` 只用于减少首次加载开销，缺失或内容不一致时从 `tasks.yaml`、`result.yaml` 重建。运行中的状态和进度不从该缓存恢复。

训练脚本内部的验证或评估仍属于 train scope，整体仍显示为训练任务的“训练/验证”阶段，不得更新测试页面任务。测试运行阶段统一为：

```text
Preparing
  -> Inference（需要重新推理时，建议映射到 0%～90%）
  -> Evaluating（C++，建议映射到 90%～98%）
  -> SavingResult（建议映射到 99%）
  -> Finished（100%）
```

仅重新评估时跳过 `Inference`，评估阶段可使用完整 0%～98% 的进度范围。Python 正常退出后任务仍是 `Running/Evaluating`；只有 C++ 评估校验完成并原子写入 `result.yaml` 后才：

1. 更新 `extra_data.test_tasks[uuid]` 可重建摘要；
2. 通知对应 `ModelEvaluationViewModel` 重新加载报告；
3. `TaskManager` 进入 `Finished` 并将总体进度设为 100。

任一评估、保存或校验步骤失败都进入 `Failed`，不能出现“任务中心 100%，测试页面仍是旧评估”的中间状态。停止操作向 Python 进程和 C++ 取消令牌同时传播。

## 17. 旧目录迁移

首次访问模型时执行幂等迁移：

```text
configs/train.yaml  -> train/config.yaml
weights/            -> train/weights/
logs/train.log      -> train/logs/train-legacy.log
logs/test.log       -> test/logs/<默认任务UUID>-legacy.log
configs/test.yaml   -> test/默认测试/config.yaml
results/pred/       -> test/默认测试/pred/
```

旧 `datasets/datasets.yaml`：

- train、validation 选择迁入训练配置；
- test 选择迁入“默认测试”；
- 旧数据集导出内容视为缓存，不做高风险拆分，下次运行时重新生成。

迁移要求：

- 目标存在时不覆盖；
- 迁移关键文件验证成功后再写 `storage_version: 2`；
- 失败时继续读取旧路径并显示迁移错误；
- 重复执行不会产生第二个默认任务；
- 旧预测结果没有标准评估报告时，页面显示“存在预测结果，但尚未生成评估报告”；
- 旧预测结果若缺少 `images.txt`、完整 `manifest.yaml` 或实际推理配置，不能承诺直接重新评估；页面提示需要重新推理以建立新协议结果。

## 18. 需要新增和修改的文件

### 18.1 建议新增

```text
src/model/include/model/ModelTestTask.h
src/model/include/model/ModelTestTaskRepository.h
src/model/ModelTestTaskRepository.cpp
src/model/include/model/ModelTestTaskManager.h
src/model/ModelTestTaskManager.cpp
src/model/include/model/ModelTaskStorageService.h
src/model/ModelTaskStorageService.cpp
src/model/include/model/ModelStorageMigration.h
src/model/ModelStorageMigration.cpp
src/model/include/model/ModelEvaluationViewModel.h
src/model/ModelEvaluationViewModel.cpp
src/model/include/model/ModelEvaluationService.h
src/model/ModelEvaluationService.cpp
src/model/include/model/ModelEvaluationModels.h
src/model/ModelEvaluationModels.cpp
src/model/include/model/ModelEvaluationProxyModels.h
src/model/ModelEvaluationProxyModels.cpp
src/model/include/model/EvaluationMetricsCalculator.h
src/model/EvaluationMetricsCalculator.cpp
src/model/include/model/EvaluationConfusionMatrixBuilder.h
src/model/EvaluationConfusionMatrixBuilder.cpp
src/model/qml/test/evaluation/*.qml
```

如果评估缩略图使用独立 Provider，还需要：

```text
src/model/include/model/EvaluationThumbnailImageProvider.h
src/model/EvaluationThumbnailImageProvider.cpp
```

### 18.2 主要修改

```text
src/common/include/common/YamlUtils.h
src/common/YamlUtils.cpp

src/project/include/project/Projects.h
src/project/Projects.cpp

src/model/include/model/TaskManager.h
src/model/TaskManager.cpp
src/model/include/model/ModelTaskController.h
src/model/ModelTaskController.cpp
src/model/include/model/ModelTaskPreparation.h
src/model/ModelTaskPreparation.cpp
src/model/include/model/ModelTaskConfigService.h
src/model/ModelTaskConfigService.cpp
src/model/include/model/ModelDatasetSelection.h
src/model/ModelDatasetSelection.cpp
src/model/ModelManager.cpp

src/data/include/data/GlobalFilter.h
src/data/GlobalFilter.cpp

src/model/qml/TestPage.qml
src/model/qml/test/TestPanel.qml
src/model/qml/test/TestTaskPanel.qml
src/model/qml/test/TestDatasetPanel.qml
src/model/qml/test/TestEvaluationPanel.qml
src/model/qml/component/ModelDelegate.qml

src/tool/qml/header/TaskCenterWindow.qml
```

普通模型 Python 预测入口需要按实际框架接入标准输出协议，首批至少完成 anomalib 的输出路径、`images.txt` 和完整 `manifest.yaml` 统一。小样本学习代码不在本方案修改范围内。

## 19. 推荐实施顺序

### 阶段 1：路径和迁移基础

- 新增 `ModelTaskStorageService`；
- 在 `common::yaml` 增加基于 `QSaveFile` 的 `writeFileAtomic()`；
- 新增目录布局版本；
- 实现幂等迁移器；
- 修改 TensorBoard 使用 `train/logs/tensorboard`。

### 阶段 2：测试任务领域模型

- 实现 `ModelTestTaskRepository`；
- 实现任务名称校验；
- 实现 `ModelTestTaskManager`；
- 为每个任务创建独立参数和数据集选择模型。

### 阶段 3：运行任务作用域

- 扩展 `TaskManager::Task`；
- 修改查找键；
- 修改 `ModelTaskController` 和 `ModelTaskRequest`；
- 修改任务中心显示名称；
- 增加 `phase` 并修正 Python 退出、C++ 评估、结果提交和 Finished 的顺序。

### 阶段 4：测试任务 QML

- 完成任务创建和切换；
- 将测试参数、数据集和开始停止操作绑定到当前任务；
- 停止使用模型级 `testDatasetViewModel` 作为测试页面状态。

### 阶段 5：标准评估协议

- 统一 `prediction_dir`；
- 输出 `images.txt`、`pred/config.yaml` 和完整 `manifest.yaml`；
- 复用 `common::yaml` 和 `yaml-cpp` 实现类型化 schema 校验及原子 YAML 写入；
- 实现推理摘要与评估摘要，支持只重新评估；
- 实现首批方法评估适配器；
- 生成包含 `instance_records` 的 `report.yaml`。

### 阶段 6：评估数据模型

- 实现指标、图表、矩阵和实例 Model；
- 实现异步加载和过滤；
- 接入两级 `QSortFilterProxyModel` 过滤链，在 C++ 后台重算过滤后的指标、矩阵和图表；
- 实现评估缩略图 Provider。

### 阶段 7：TestEvaluationPanel

- 实现上下 SplitView；
- 实现上部三栏；
- 实现下部三栏；
- 接入矩阵、GridView、详情联动；
- 完成空状态、加载状态和错误状态。

### 阶段 8：构建和回归

- 单元测试；
- QML 组件测试；
- 旧项目迁移测试；
- Release 构建；
- anomalib 普通模型测试实际运行验证。

## 20. 测试与验收标准

### 20.1 多测试任务

- 同一模型能创建至少两个测试任务；
- 参数、数据集、结果和评估报告互不覆盖；
- 切换任务后饼图和参数立即更新；
- 重新打开项目后恢复任务列表和上次选择；
- 任务中心显示具体测试任务名称；
- 运行任务 A 时切换到任务 B，不影响 A 执行。

### 20.2 路径

- 训练配置只写入 `train/config.yaml`；
- 训练权重只写入 `train/weights`；
- 训练日志只写入 `train/logs`；
- 测试配置写入 `test/<任务名>/config.yaml`；
- 测试日志只写入 `test/logs`；
- 预测结果准确写入 `test/<任务名>/pred`；
- `pred/images.txt` 覆盖全部实际推理图像，格式为 `image_id,image_path`；
- `pred/manifest.yaml` 保留重新计算阈值、PR/ROC 所需的完整 score；
- 预测和评估统一使用 `manifest.yaml`、`report.yaml`；报告 schema 固定为 3；
- C++ 通过 `common::yaml` 和 `yaml-cpp` 读写并校验协议，不在 model 模块复制 YAML 工具；
- `result.yaml` 和索引文件使用 `QSaveFile` 原子提交，模拟写入失败时旧文件不被截断；
- 不出现 `pred/pred`；
- 新代码不再写入 `configs/`、`results/`、根 `weights/` 和根 `logs/`。

### 20.3 重跑与重新评估

- 数据集、checkpoint 或推理参数变化后，重新运行会先清空当前任务旧 `pred/`，不残留上次文件；
- 每个测试任务始终只有一份当前 PRED，不生成历史运行目录；
- 完整推理失败后没有 `result.yaml`，页面不会加载半成品为有效结果；
- 只修改 confidence、IoU、匹配策略或其他评估参数时不启动 Python，保留 `pred/` 并重建 `evaluation/`；
- GT 标注或类别定义改变时可以复用 PRED 重新评估；
- 修改全局过滤栏既不重新推理，也不改写磁盘评估；
- 当前配置与结果不一致时，页面能区分“需要重新推理”和“只需重新评估”。

### 20.4 指标和矩阵

- 对角线数量等于正确类别匹配数；
- 非对角线数量等于类别错误匹配数；
- FP 列只包含未匹配 PRED；
- FN 行只包含未匹配 GT；
- 每个类别的 TP、FP、FN 与指标公式一致；
- micro Precision、Recall、F1 可用人工小样本验证；
- 官方指标和诊断指标分开保存，顶部明确显示当前口径；
- PR/ROC 从完整 PRED 计算，不从固定工作点事件反推；
- 零分母显示 `—`；
- 矩阵行列始终为类别数 + 2。

### 20.5 联动

- 点击任意类别组合单元格，GridView 只显示该组合；
- 点击 FP/FN 单元格，GridView 只显示对应错误实例；
- 点击汇总行列能显示正确聚合结果；
- GridView 切换选中项后，详情同步更新；
- 切换测试任务后不会残留上一任务的矩阵过滤和实例详情。

### 20.6 全局过滤

- 启用数据集过滤后，指标、矩阵、图表和 GridView 同时只统计命中数据集；
- 标签类别过滤正确处理匹配对、FP 和 FN；
- Tag、ImageLabelClass、文件名和 Custom 条件与数据页面语义一致；
- 反选、空选择和清空过滤条件行为与 `GlobalFilter` 一致；
- 全局过滤结果始终与测试任务原始数据集范围取交集；
- 快速连续切换过滤条件时，过期后台结果不会覆盖最新结果；
- 工作线程不访问 ProxyModel、`QModelIndex` 或 QML 对象；
- 大型 YAML 在工作线程完整解析，释放 `YAML::Node` 后再提交 Qt Model，GUI 线程不出现解析卡顿；
- 清空过滤后恢复完整报告的指标和事件数量；
- 过滤不修改 `report.yaml`、原始 PRED 或测试任务配置；
- QML 中不存在对评估事件的 JavaScript 遍历、聚合或指标计算。

### 20.7 缩略图

- GT 使用实线；
- PRED 使用虚线；
- 二者使用同一个坐标映射；
- 裁剪范围是 GT/PRED bounds 并集；
- FP、FN 单边实例仍能正确裁剪；
- 边界目标不会裁剪到图像外；
- 重跑或重新评估后不会命中旧缩略图缓存；
- 大量实例滚动时不一次性创建所有 delegate。

### 20.8 扩展性与布局

- 异常检测可以只通过 adapter 提供分数分布图；
- 目标检测可以只通过 adapter 提供 PR 曲线；
- 新增图表不需要修改 `TestEvaluationPanel.qml`；
- 缺少某项能力时相应面板显示明确空状态，不崩溃或显示伪数据；
- 普通模型测试始终只有一个测试数据集选择；
- 常见窗口尺寸下设置区可折叠，评估上下两部分不会因硬最小高度被裁掉。

## 21. 已确定的关键决策

1. 测试任务名称经过严格校验后直接作为目录名，稳定身份使用 UUID；
2. 测试任务定义与 `TaskManager` 运行实例分离；
3. 测试配置、数据集、预测和评估结果按任务隔离；
4. 测试日志集中在 `test/logs`，每个测试任务使用固定 UUID 文件名，不引入持久运行 ID；
5. 训练权重是测试任务的只读输入，统一位于 `train/weights`；
6. 混淆矩阵行为 PRED、列为 GT，额外增加 FP 列、FN 行和合计行列；
7. 实例缩略图以 GT/PRED bounds 并集裁剪，GT 实线、PRED 虚线；
8. 评估页面使用独立评估事件模型，不复用项目标注实例模型；
9. 图表通过通用 descriptor 和 `QuiChart` 展示，不在 QML 中绑定算法；
10. 实时任务进度以 `TaskManager` 为唯一事实来源；Python 退出不等于测试完成，提交 `result.yaml` 后才进入 100%；
11. 数据集、标签类别、Tag、文件名和 Custom 全局过滤条件同时作用于指标、矩阵、图表和实例列表；
12. 推理后的匹配、指标、过滤聚合、矩阵和图表数据由 C++ 后台完成，QML 只展示 Model 和转发交互；
13. 每个测试任务只有一份当前 PRED，完整重跑前清空；`images.txt` 明确当前推理图像全集；
14. 推理参数和评估参数分开判定，只改评估参数或 GT 时复用 PRED，不重新运行 Python；
15. 普通模型测试只有单一测试数据集选择，小样本学习不纳入该测试任务架构；
16. `QSortFilterProxyModel` 在 GUI 线程完成过滤，后台聚合只读取稳定 ID/序号和只读纯值记录，不新增一组 Snapshot 类型；
17. 持久配置和结果路径使用受根目录约束的相对路径，Windows 大小写重命名经过临时中间名；
18. 结构化持久文件统一使用 YAML，C++ 复用 `common::yaml` 和 `yaml-cpp`；`images.txt` 仅承担 `image_id,image_path` 图像清单职责；
19. YAML 单文档在工作线程完整解析为类型化 C++ 记录，Qt Model 只虚拟化展示，不伪装成文件偏移懒加载。
