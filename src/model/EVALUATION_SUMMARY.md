# `src/model` 评估代码概况

## 总体结论

测试评估采用一条简单的数据流：Python 只负责推理，C++ 每次需要评估时读取任务级
`test.txt`、任务数据库预测记录/文件和项目数据库中的图像、类别、标注并计算，Qt Model
保存当前进程中的展示数据，QML 只负责显示和交互。评估结果不写入磁盘。

```text
选中测试任务
    ↓（惰性触发）
ModelTestTaskManager
    ↓
ModelEvaluationViewModel::evaluate()（QThreadPool）
    ↓
ModelEvaluationService
    ├─ 读取 test/<任务名称>/test.txt
    ├─ 读取 task.db 中的数据集选择和预测记录
    ├─ 读取项目数据库中的图像、类别和标注
    ├─ 读取 pred/ 中的预测文件
    └─ C++ 构造 GT，计算指标、矩阵、事件和方法图表
    ↓
ModelEvaluationViewModel（内存缓存）
    ↓
现有 Qt Model / QML 评估面板
```

## 文件职责

模型和测试任务持久化数据库、文件列表和推理产物：

```text
models/<模型名>/
├─ model.db                    # 模型参数、训练/验证选择和测试任务索引
├─ datasets/
│  ├─ train.txt
│  ├─ validation.txt
│  ├─ train_labels.json
│  └─ validation_labels.json
└─ test/<任务目录>/
   ├─ task.db                  # 测试参数、测试数据集/类别选择和预测记录
   ├─ test.txt                 # 当前测试任务的图像列表
   └─ pred/                    # Python 推理产物
```

测试任务不额外生成测试标签旁路文件。评估时由 C++ 根据 `task.db` 的数据集/类别选择，
重新从项目数据库获取图像、图像类别和标注；缺失源图像跳过，缺失预测按空预测参与评估。
这使首次测试、修改评估参数重新评估和重新打开项目后的评估使用同一条评估路径。

异常检测的预测产物示例：

```text
test/<任务目录>/pred/
└─ <image_id>.tiff             # 原始异常分数图（如有）
```

评估指标、混淆矩阵、图表、图像记录和实例事件只存在于
`ModelEvaluationViewModel` 及其 Qt Model 中。进程结束后不保留评估快照，重新打开
项目时从上述输入重新评估，不读取旧评估结果，也不做旧协议兼容。

## 三种触发场景

### 用户首次开始测试或再次开始测试

`ModelTaskController` 只处理完整推理链：

1. 后台导出当前数据集并生成图像列表；
2. 清理当前任务的 `pred/`，重新写入任务数据库中的推理参数；
3. 启动 Python 模型推理；
4. 推理完成后，测试评估页面按需读取新的 PRED 并调用 C++ 评估。

每次用户点击开始都重新推理，不依据旧预测产物的摘要决定是否复用。推理失败或被
停止时，当前 ViewModel 清空，不展示上一次评估结果。

### 修改评估参数

confidence、IoU、匹配策略或其他评估组参数变化时，不启动 Python。管理器使当前
ViewModel 失效，评估线程重新读取 `test.txt`、数据库和预测产物，再生成一份新的内存结果。
旧结果会先清空，失败时页面保持无结果。

### 切换任务或重新打开项目

管理器按模型 UUID 和测试任务 UUID 缓存 ViewModel。同一进程内切换任务时，已经成功
评估且输入未变化的对象直接复用，不重复计算。删除任务时同步删除对应缓存。

重新打开项目后缓存为空；只有在评估页面绑定到选中的任务时才惰性评估。打开行为不
弹窗，只有由用户点击开始测试产生的评估完成/失败才调用
`ui::SignalHelper::notifySuccess` 或 `ui::SignalHelper::notifyError`。

## 主要模块

| 模块 | 职责 |
| --- | --- |
| `ModelTaskController` | 导出数据、生成任务级 `test.txt`、清理并生成预测产物、启动 Python；不执行评估。 |
| `ModelTaskPreparation` | 在后台准备任务目录、数据库、文件列表和外部进程规格。 |
| `ModelEvaluationService` | 在工作线程从项目/任务数据库和预测产物构造 GT/PRED，执行匹配、指标、矩阵、事件和可扩展图表计算。 |
| `ModelEvaluationViewModel` | 管理异步生命周期，接收内存结果，填充 Qt Model，处理选择和过滤。 |
| `ModelTestTaskManager` | 管理任务定义、ViewModel 内存缓存、惰性触发和通知来源。 |
| `ModelStorageService` | 统一提供测试任务、数据集和 PRED 路径。 |
| `TestEvaluationPanel.qml` | 只负责评估页面布局、状态空态和组件联动。 |

## 后台与状态规则

- `ModelEvaluationService` 只接收路径和普通值，不访问 `QObject`、`QModelIndex`、QML
  或 `DataManager`；数据库、预测文件解析和指标计算在 `QThreadPool` 中执行。
- ViewModel 在开始新评估时先清空已有 Model 数据；后台结果返回时用 revision 丢弃
  过期任务，避免旧线程覆盖新参数的结果。
- 评估失败会记录错误、清空结果并进入错误状态；不会继续展示成功评估留下的内容。
- 找不到数据集源图像时跳过该图像；找不到某图像的预测时按空预测评估。缺失数量和
  被忽略记录由 `spdlog` 汇总警告，不逐图像打印。
- 运行中的测试显示运行状态，Python 完成后才进入评估流程；评估完成不是 Python
  任务的第二个持久化阶段。

## 评估内容和图表

检测/分割方法由 C++ 生成诊断匹配、类别指标、混淆矩阵和 PR 曲线所需的 descriptor。
异常检测按图像级 `GOOD`/`Anomaly` 处理，使用原有图像级 `pred_score`（每张图像预测
结果中的最大 score）生成分数分布图：横轴是分数，纵轴是数量，曲线下填充颜色；GOOD
使用绿色，Anomaly 使用红色。存在对应类别时显示该类别的最大/最小分数竖直虚线，
但图例只显示 GOOD 和 Anomaly。

图表通过通用 descriptor 传递 `kind`、`chart_id`、`data` 和 `options`，由
`EvaluationChartPanel.qml` 选择并交给 `QuiChart`。因此后续可以为目标检测保留 PR
曲线，为其他方法增加新的图表类型，而不复制评估页面结构。

## 过滤和缓存

全局数据集、类别、状态、矩阵单元和分数过滤仍由 Qt Proxy Model 在 GUI 线程确定可见
记录；需要重算指标时只复制脱离 Qt Model 的普通值，交给后台重聚合。过滤结果不是
持久化评估结果，也不改变原始 PRED。

评估缓存只按任务身份复用成功的 ViewModel。完整推理输入变化由下一次显式开始测试
触发清空；评估参数变化立即失效并重新评估。系统不维护输入、图像列表、GT、推理或
评估的校验摘要，也不把这些摘要用作失败判定。

## 破坏性协议边界

当前实现只接受新的模型数据库/任务数据库、任务级文件列表和预测产物布局。旧的 YAML
配置、manifest、评估结果文件、旧评估专用任务路径和旧字段不转换、不加载；用户需要
重新开始测试生成新的推理产物。评估本身不新增持久文件。

## 静态验证

- `cmake --build build --config Release -- /m:4` 已成功。
- `git diff --check` 已通过。
- 未添加或运行额外测试。
