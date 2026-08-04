# 模型测试与评估架构

> 状态：按当前实现维护
> 日期：2026-08-04

## 1. 设计边界

普通模型测试只有一条数据流：用户主动开始时导出任务级图像列表并调用 Python
推理；评估页面选中任务后，C++ 在后台从数据库和预测产物重新评估。推理结果保留在
文件系统和任务数据库中，评估结果只保留在进程内存中。

```text
用户点击开始
    ↓
后台导出数据并写入 task.db
    ↓
test/<任务名称>/test.txt + Python 推理
    ↓
评估页面选中任务
    ↓
ModelEvaluationService（QThreadPool）
    ↓
ModelEvaluationViewModel（内存缓存）
    ↓
QML / QuiChart
```

项目重新打开后缓存为空，只有评估页面选中任务才重新评估；打开、切换页面和缓存命中
不弹窗。只有用户主动开始测试触发的评估完成或失败才发送通知。

## 2. 持久化布局

```text
models/<模型名>/
├─ model.db
├─ datasets/
│  ├─ train.txt
│  ├─ validation.txt
│  ├─ train_labels.json
│  └─ validation_labels.json
├─ train/
│  ├─ weights/
│  └─ logs/
└─ test/
   ├─ logs/<测试任务 UUID>.log
   └─ <任务名称>/
      ├─ task.db
      ├─ test.txt
      └─ pred/
         └─ <预测文件>
```

`datasets/` 是模型共享目录，不按训练/测试任务重复创建。测试图像列表是任务私有的
`test.txt`，每行包含 `image_id,image_path`。测试不生成测试标签 JSON、数据集
manifest、预测 manifest、配置文件或评估报告；TIFF/PNG、权重和日志继续保留在文件系统。

### model.db

```text
train_params(name_en, value)
datasets(type, dataset_id, class_ids)
test_tasks(task_id, name, ctime, mtime)
```

保存模型级训练参数、训练/验证/测试选择和测试任务索引。任务目录由
`test/<任务名称>/` 组合得到，不保存目录或运行状态。

### task.db

```text
task_info(task_id, ctime, mtime)
test_params(name_en, value)
datasets(type, dataset_id, class_ids)
prediction(image_id, data)
```

保存测试参数、数据集/类别选择和 Python 推理写入的预测记录。`prediction.data` 按方法
解析：异常检测使用 `image_score`；目标检测可以使用 `score` 与 `x,y,w,h`，后续方法
通过同一数据字段扩展协议。TIFF 路径由 `pred/<image_id>.tiff` 组合，不写入数据库。

## 3. 测试生命周期

### 首次测试或再次点击开始

1. 后台根据当前数据集选择导出 `test.txt`。
2. 清空任务 `prediction` 表并清理任务 `pred/`，随后重新生成文件列表。
3. 启动 Python 模型推理。
4. 推理完成后，评估页面按需调用 C++ 评估。

每次主动开始都重新推理，不通过 digest 或旧结果判断是否复用。推理失败、停止或
准备失败时，当前评估模型清空，不展示上一次结果。

### 修改评估参数

confidence、IoU、匹配策略等评估参数变化时，不重新推理。当前内存结果失效，C++
后台重新读取 `test.txt`、`task.db`、项目数据库和预测产物进行评估。

### 切换任务和重新打开

评估缓存按模型 UUID 与测试任务 UUID 保存。同一进程内切换到已成功评估且参数未变的
任务时复用缓存；参数变化才重新计算。重新打开项目后缓存为空，选中任务时重新读取
数据库中的数据集/类别选择和项目数据库中的图像与标签，再进行评估。

## 4. C++ 评估输入

`ModelEvaluationService` 不访问 `QObject`、QML 或 `DataManager`，只接收路径和值类型：

```text
test.txt
    ↓ 读取 image_id
task.db.datasets
    ↓ 获取当前选择
项目数据库
    ↓ 获取图像、类别和标注
内存 GT
    + task.db.prediction
    ↓
内存 PRED → 匹配、指标、矩阵、事件和图表
```

- 文件列表中的图像在项目数据库中不存在时跳过；源文件不存在时跳过。
- 找不到某图像的预测时按空预测评估。
- 缺失图像、缺失预测和被忽略预测只按汇总数量写入 `spdlog` warning，不逐图像刷屏。
- 评估失败先清空旧 Model 数据，页面进入无结果/失败状态。

## 5. 图表扩展

图表通过统一 descriptor 传给 `EvaluationChartPanel.qml`，QML 只负责选择并展示：

```text
{
  kind: "line" | "bar",
  chart_id: "...",
  title: "...",
  data: { labels: [...], datasets: [...] },
  options: { ... }
}
```

异常检测使用 LineChart 分数分布图：横轴为原有图像级 `pred_score`，纵轴为数量，
GOOD 绿色填充、Anomaly 红色填充；有对应类别时绘制 GOOD 最大分数和 Anomaly 最小
分数竖直虚线，虚线不进入图例。目标检测后续可增加 PR 曲线 descriptor，不改变评估
页面总体结构。

## 6. 破坏性协议边界

当前实现不读取旧 YAML 配置、旧 manifest、旧结果报告、旧 digest 或旧评估目录，也不
做版本判断和兼容转换。需要重新开始测试生成新的任务数据库、文件列表和预测产物。

FS-SAM2 小样本流程使用相同的模型级/任务级数据库和共享数据集布局，但不接入普通
评估页面。
