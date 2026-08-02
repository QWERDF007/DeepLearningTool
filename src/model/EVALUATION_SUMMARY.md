# `src/model` 评估代码概况

## 总体结论

评估链路采用“Python 生成预测、C++ 计算评估、Qt Model 提供内存数据、QML 展示”的分层设计。
本次整理将评估结果收敛为一份报告和一份轻量提交摘要，避免同一批实例事件在多个 YAML 和多个缓存副本中重复保存。

```text
ModelTestTaskManager
        ↓
ModelTaskController
        ↓
pred/{config.yaml, images.txt, manifest.yaml}
        ↓
ModelEvaluationService（后台线程）
        ↓
evaluation/report.yaml + result.yaml
        ↓
ModelEvaluationViewModel
        ↓
现有 Qt Model / QML 评估面板
```

## 主要模块

| 模块 | 职责 |
|---|---|
| `ModelEvaluationProtocol` | 集中管理评估方法、状态、匹配策略、指标集合、矩阵单元和报告字段协议。 |
| `ModelEvaluationService` | 校验 GT/PRED，执行 IoU 匹配，计算指标、混淆矩阵和图表，并原子写入报告。 |
| `ModelEvaluationModels` | 持有指标、图像、实例、矩阵和图表的 Qt Model；这些 Model 是页面运行期的唯一数据缓存。 |
| `ModelEvaluationViewModel` | 异步校验并加载报告，提供过滤、选择和过滤后的重聚合。 |
| `ModelTaskController` | 构造评估参数、复用 PRED、启动/取消评估并最后提交 `result.yaml`。 |
| `ModelTestTaskManager` | 管理测试任务，并一次性绑定当前任务的报告/结果路径。 |
| `ModelStorageService` | 提供测试任务、PRED、报告和结果摘要路径。 |

## 输入与输出

输入：

```text
datasets/.../manifest.yaml
pred/config.yaml
pred/images.txt
pred/manifest.yaml
```

新版本输出：

```text
evaluation/report.yaml   # 评估配置、指标、矩阵、图表、图像记录和 instance_records
result.yaml              # 轻量摘要和 finished 提交标志
```

`report.yaml` 包含：

- 模型、测试任务、评估方法和 schema 版本；
- 推理、输入、GT、权重和评估 digest；
- 评估阈值、IoU 匹配策略和能力声明；
- diagnostic / official 指标、类别目录、混淆矩阵和图表；
- `image_records`：完整测试图像、GT 和原始预测，用于图像指标及过滤重算；
- `instance_records`：匹配、误检和漏检事件；
- PRED 与 GT manifest 的受控相对路径引用。

不再写入 `evaluation/config.yaml`、`evaluation/instances.yaml`，也不再额外写一份
`prediction_records`；原始预测已经包含在对应的 `image_records.predictions` 中。
只有 schema 3 的 `evaluation/report.yaml` 作为有效评估报告。

## 核心评估逻辑

### 协议校验

`ModelEvaluationService` 校验：

- `pred/images.txt` 的 CSV、图像 ID、路径和完整覆盖关系；
- `pred/manifest.yaml` 的 schema、记录数、元数据、预测 ID、类别 ID 和 score；
- bbox、polygon、mask 等 geometry 及受控 artifact 路径；
- 当前模型、测试任务和评估方法的一致性。

YAML 文件大小和记录数量均有上限。

### 匹配和指标

默认值为 confidence `0.5`、IoU `0.5`、`greedy_iou`。协议层枚举同时支持
`greedy_iou` 和 `hungarian_iou`。

检测/分割任务计算：

- true positive、class mismatch、false positive、false negative；
- overall 与 per-class Precision、Recall、F1 及 TP/FP/FN；
- 图像级指标和混淆矩阵；
- 按类别指标柱状图和 PR 曲线。

异常检测按图像级 `GOOD`/`Anomaly` 二分类处理，保留正常图像的 true negative，生成异常分数图。

## 内存与并发精简

- `ModelTaskRequest::evaluation_method` 和 `ModelEvaluationOptions::method` 使用评估枚举，只有写入外部 YAML 时转为字符串。
- `ModelEvaluationResult` 只返回计数和 digest，不再携带重复的 `QVariantMap` 摘要。
- ViewModel 不保存 `Snapshot`、完整报告副本、实例路径副本、类别目录副本或图表描述副本；
  指标、图像、实例、矩阵和图表 Model 是唯一页面内存数据源。
- 报告和结果文件只按路径、大小和修改时间判断是否需要重新解析；内容未变化时不会重复加载 YAML。
- 当前测试任务通过 `setPaths()` 一次性绑定 report/result，避免两个 setter 连续触发两次 reload。
- 过滤重聚合的后台线程只接收必要的脱离 Qt Model 的计算输入，不保存为长期缓存；旧任务结果或运行历史不建立快照。

## ViewModel 与 QML

ViewModel 负责：

- 以 `result.yaml` 作为有效结果提交门槛；
- 异步读取并校验新版 `report.yaml`；
- 只读取报告内的 `instance_records`；
- 暴露指标、矩阵、图表、图像和实例 Qt Model；
- 处理全局过滤、状态/类别/score 过滤、矩阵联动和实例选择；
- 识别推理过期与评估过期。

QML 只消费现有 Model 和 role，评估页面布局与尺寸保持不变。

## 文件关系

```text
pred/config.yaml       ─┐
pred/images.txt         ├─> ModelEvaluationService ─> evaluation/report.yaml
pred/manifest.yaml      ┘                              │
datasets/manifest.yaml ────────────────────────────────┘
                                                       ↓
                                             result.yaml（最后原子提交）
```

`result.yaml` 写入失败时，控制器清理本次生成的 `evaluation/`，避免半成品报告被当作有效结果。
重新评估可以复用完整 PRED；推理输入或权重变化时，控制器会重新生成 PRED。

## 扩展注意事项

新增评估方法时，需要同步更新：

1. `ModelEvaluationProtocol` 的方法映射和能力判断；
2. GT/PRED manifest 适配及 geometry 校验；
3. 指标、匹配策略和报告字段；
4. 必要的 Qt Model role 和 QML 展示能力。

FS-SAM2 仍是独立的小样本流程，不接入普通模型测试评估适配器。

## 当前重构进度

### 已完成

- 评估协议集中到 `ModelEvaluationProtocol.h/.cpp`，报告 schema 固定为 3。
- 结果输出收敛为 `evaluation/report.yaml` 和 `result.yaml`，旧的评估配置、实例文件、旧报告 schema 和重复预测记录不再兼容。
- `image_records` 只保存图像元数据、GT 实例和原始预测；实例事件使用扁平结构，通过 `image_id` 关联图像。
- 删除实例代理模型、重复字段、冗余快照和长期缓存，类别 ID、最高分和 GT/PRED 存在性在运行时派生。
- 增加严格的报告、结果、图像记录、预测记录和实例事件校验。
- 评估阶段不再读取或计算权重摘要，也不再把 checkpoint 校验写入评估报告或结果摘要。
- 新评估会跳过不存在的源图像；加载已有报告时，数据集或 `images.txt` 版本变化会隐藏旧详情，单张源图缺失时只隐藏该图及关联事件。

### 尚未完成

- 最新的权重和数据可用性修改尚未重新编译；最近一次编译被中断。
- `docs/MODEL_TESTING_ARCHITECTURE.md` 仍有旧的权重字段、评估路径、事件结构、矩阵示例和 manifest 回写描述，需要同步到当前协议。
- 需要确认详情被过滤后，顶部指标是否也应清空或按可用详情重新聚合，避免展示无法对应当前数据的旧汇总值。
- 需要验证权重替换、数据集 manifest 修改/删除、`images.txt` 修改、单张图像删除等场景。
- `weight_digest` 仍保留在预测配置和 PRED 复用流程中，用于推理阶段；如果要求权重变化连 PRED 复用判断也不影响，还需继续调整 `inferenceDigest()`。

### 后续步骤

1. 编译 `dltool_model`，修复最新修改引入的编译问题。
2. 编译完整工程并检查 QML/Model 链路。
3. 更新 `docs/MODEL_TESTING_ARCHITECTURE.md`，删除旧评估协议描述。
4. 验证数据缺失和权重变化场景，确认无效图像不会进入图像或实例 Model。
5. 执行残留协议搜索、`git diff --check` 和最终工作树检查。
