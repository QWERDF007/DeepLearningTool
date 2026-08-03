# 模型测试任务与评估架构

> 状态：按当前实现维护
> 日期：2026-08-03
> 适用范围：`src/model/`、模型推理任务和测试页面 QML

## 1. 设计结论

普通模型测试由两个阶段组成：用户主动开始测试时导出数据并调用 Python 推理；
测试评估页面选中任务后，才由 C++ 在后台读取预测结果并计算。推理产物持久化，
评估结果只存在当前进程的内存中。

```text
测试任务配置 + 数据集选择
          │
          ├─ 用户点击开始
          │      ↓
          │  后台导出图像列表 → 清理 pred/ → Python 推理
          │                                      ↓
          └─ 评估页面选中任务 ← pred/images.txt + pred/manifest.yaml
                                      ↓
                         ModelEvaluationService（后台 C++）
                                      ↓
                         ModelEvaluationViewModel（内存）
                                      ↓
                                  QML / QuiChart
```

这个边界有三个直接结果：

- 重新打开项目时不读取旧评估快照，而是按需重新读取 PRED 并评估；
- 评估参数变化不重新推理，只重新读取 PRED 评估；
- UI 触发评估不会被 YAML 解析和指标计算阻塞。

## 2. 持久化范围

测试任务定义由 `test/tasks.yaml` 和每个任务目录下的 `config.yaml` 保存。推理产物
使用如下布局：

```text
models/<模型名>/
├─ train/
│  ├─ config.yaml
│  ├─ datasets/
│  ├─ weights/
│  └─ logs/
└─ test/
   ├─ tasks.yaml
   ├─ logs/<测试任务 UUID>.log
   └─ <测试任务目录>/
      ├─ config.yaml
      ├─ datasets/
      │  └─ manifest.yaml
      └─ pred/
         ├─ config.yaml
         ├─ images.txt
         ├─ manifest.yaml
         └─ <预测文件或 artifact>
```

文件职责如下：

| 文件 | 职责 |
| --- | --- |
| `test/tasks.yaml` | 测试任务索引、顺序和当前选中任务。 |
| `<任务>/config.yaml` | 当前测试参数和数据集选择。 |
| `<任务>/datasets/manifest.yaml` | 当前测试使用的数据集、图像和 GT。 |
| `<任务>/pred/config.yaml` | 本次推理实际使用的配置。 |
| `<任务>/pred/images.txt` | 导出的图像列表，包含没有预测实例的图像。 |
| `<任务>/pred/manifest.yaml` | Python 生成的规范化预测和 score。 |

评估指标、矩阵、图像记录、实例事件和图表 descriptor 不新增持久化文件。进程退出
后这些内存数据被丢弃；项目重开后只有在用户进入对应评估页面并选中任务时才重新
建立。

路径只能由 `ModelStorageService` 生成。任务目录名经过校验，解析产物路径时必须
确认最终路径仍位于模型或测试任务根目录内；不得在控制器、评估服务或 QML 中自行
拼接模型存储目录。

## 3. 测试任务和运行任务

### 3.1 测试任务定义

`ModelTestTaskDefinition` 是长期保存的用户配置，包含：

- UUID、名称、目录名和创建/修改时间；
- 测试参数；
- 一个 `ModelDatasetSelection` 数据集选择。

同一模型下名称不区分大小写且不能重复。UUID 用于缓存和任务作用域，名称只用于
界面展示，目录名只用于文件布局。

### 3.2 运行任务

`TaskManager::Task` 是当前进程内的运行状态，使用模型 UUID、任务类型和测试任务
UUID 区分普通测试任务。它负责状态、进度、TCP 事件和停止请求，不保存评估快照。

```text
Pending → Preparing → Running → Finished
                    └→ Stopping → Stopped
                    └→ Failed
```

`Preparing` 包含数据集导出、目录准备、配置写入和进程规格生成；Python 真正启动
后才进入 `Running`。Python 完成只表示推理结束，评估页面之后才会惰性启动 C++
评估。评估不改变 Python 运行任务的完成状态。

## 4. 推理执行链

`ModelTaskController` 是模型任务的唯一启动入口：

1. `startModelTestTask()` 建立或复用当前测试任务的运行记录；
2. `TaskManager` 接受用户开始请求并发出 `taskStartRequested`；
3. `ModelTaskPreparation` 在后台导出数据集并生成图像列表；
4. 准备阶段清理当前任务的 `pred/`，重新生成 `pred/config.yaml` 和
   `pred/images.txt`；
5. `ExternalModelTaskRunner` 启动 Python；
6. Python 将预测写入 `pred/manifest.yaml` 及相关 artifact，并通过 TCP 上报状态；
7. Python 成功退出后任务进入 `Finished`，评估管理器等待页面选中任务。

每次用户主动点击开始都重新执行第 3～6 步，不依赖旧预测产物是否存在。推理失败、
停止或准备失败时，当前评估 ViewModel 会清空，旧评估数据不会继续显示。

数据集导出和配置生成使用后台工作流。后台输入是纯值结构，不包含 `QObject`、
`DataManager`、`IModel` 或数据库对象。

## 5. 惰性评估生命周期

### 5.1 绑定任务

`ModelTestTaskManager` 在测试评估页面绑定当前任务时：

1. 从任务定义、模型记录和 `ModelStorageService` 组装 `ModelEvaluationOptions`；
2. 取得该模型 UUID 与测试任务 UUID 对应的 ViewModel；
3. 若该 ViewModel 没有相同输入的成功结果，则调用 `evaluate(false)`；
4. 评估在 `QThreadPool` 中运行，GUI 线程只接收最终普通值并刷新 Qt Model。

没有选中任务时不启动评估。项目重新打开后缓存为空，因此选中任务会重新读取当前
数据集 manifest 和 PRED；这不会弹出成功或失败通知。

### 5.2 评估参数变化

评估参数组统一经过 `normalizedEvaluationConfig()` 规范化。confidence、IoU、
匹配策略及其他评估组字段变化会：

- 取消旧的后台评估；
- 清空当前显示数据；
- 使用同一组 PRED 路径再次读取并评估；
- 结果失败时保持无结果状态。

推理参数或数据集选择变化只使当前结果失效，不自动启动 Python；用户下一次点击
开始测试时才重新导出、推理和评估。

### 5.3 内存缓存

缓存键为模型 UUID 与测试任务 UUID。缓存的值是该任务的
`ModelEvaluationViewModel`，不是文件快照，也不包含跨进程数据。任务切换时复用
已经成功且输入未变化的对象；删除任务时同步释放缓存。评估输入或评估参数变化
会让对象失效并重新计算。

后台结果携带 revision。参数快速变化或任务切换后，过期线程的返回值不会覆盖当前
对象。ViewModel 在启动新评估和接收到失败时都会先清空指标、矩阵、图表、图像和
实例 Model，因此不会残留上一次结果。

## 6. C++ 评估服务

`ModelEvaluationService` 只接收 `ModelEvaluationOptions` 和取消标志，主要路径为：

```text
dataset manifest ─┐
pred/images.txt   ├─> 读取并关联图像
pred/manifest.yaml┘
                         ↓
                GT / PRED 统一内存记录
                         ↓
        匹配、诊断指标、官方指标、矩阵、事件、图表
```

服务负责：

- 校验 manifest 类型、schema、记录数量、图像 ID、路径、geometry 和 score；
- 根据数据集 manifest 读取 GT，根据 PRED manifest 读取预测；
- 对缺失源图像跳过评估；
- 对缺失预测的图像按空预测处理；
- 按方法执行图像级或实例级匹配和指标计算；
- 返回一份 `ModelEvaluationResult::evaluation_data` 内存 map。

找不到的图像和无预测记录只在日志中按数量汇总。评估服务不访问 QML Model，也不
写入评估文件。

## 7. 指标、事件和图表

诊断指标用于当前页面的混淆矩阵、实例事件和过滤重聚合；官方指标用于方法定义的
正式工作点或曲线。二者在内存数据中分开保存，顶部卡片根据
`primary_metric_set` 选择展示口径。

方法图表统一使用 descriptor：

```text
{
  kind: "line" | "bar",
  chart_id: "...",
  title: "...",
  data: { labels: [...], datasets: [...] },
  options: { ... }
}
```

`EvaluationChartPanel.qml` 只根据 descriptor 的 kind 创建 `QuiChart`，不根据具体
方法复制布局。当前能力包括：

- 异常检测：LineChart 分数分布图，横轴为原有图像级 `pred_score`，纵轴为数量；
  GOOD 使用绿色填充，Anomaly 使用红色填充；存在对应类别时绘制 GOOD 最大分数和
  Anomaly 最小分数的竖直虚线，图例只保留两个分布类别；
- 目标检测/分割：按类别指标柱状图和可扩展的 PR 曲线 descriptor；
- 后续方法可以增加新的 `chart_id` 和 `kind`，不需要改变测试页面的总体结构。

异常检测图表中的分箱数量由 C++ 分布计算统一设置，QML 不参与分箱、计数或阈值
计算。图表仅使用当前评估内存记录，不从另一个报告或旧文件加载数据。

## 8. Qt Model 与 QML

`ModelEvaluationViewModel` 暴露以下内存模型：

- 总体和类别指标；
- 混淆矩阵；
- 图像和实例记录；
- 图表 descriptor；
- 过滤代理和当前选中事件。

QML 只负责：

- 展示 Loading、Running、Failed、MissingResult 和 Ready 状态；
- 绑定 Qt Model 的 roles；
- 转发矩阵、实例和过滤交互；
- 将 chart descriptor 传给 `QuiChart`。

QML 不遍历预测文件，不计算 IoU、TP/FP/FN、分箱或指标。全局过滤的可见范围由
Qt Proxy Model 在 GUI 线程确定；过滤后的聚合输入会复制为普通值，再在后台线程
重算。过滤不会改变原始 PRED 或任务配置。

## 9. 日志和通知

日志使用 `spdlog`：

- 评估失败打印任务级错误；
- 源图像缺失、预测缺失和被忽略的记录按数量汇总 warning；
- 不逐图像打印，避免大数据集产生过多日志。

通知只属于用户主动开始测试的链路。管理器在接受 `taskStartRequested` 时记录任务
身份，之后该次评估完成调用 `ui::SignalHelper::notifySuccess`，失败调用
`ui::SignalHelper::notifyError`。项目打开、页面切换、缓存命中和评估参数修改均不
主动弹窗。

## 10. 破坏性边界和兼容策略

当前代码只接受新的数据集/PRED manifest 结构。旧的评估专用目录、旧评估字段和旧的
磁盘评估数据不转换、不加载；需要重新点击开始测试生成新的推理产物。测试任务配置、
数据集 manifest、PRED 配置和 PRED manifest 仍按现有 YAML 协议保存，评估本身不新增
持久化输出。

系统不保存或比较输入、图像列表、GT、推理和评估的校验摘要，也不以这些摘要决定
是否失败。评估是否需要执行只由页面惰性绑定、内存缓存和评估参数变化决定。

## 11. 扩展规则

新增模型方法时按以下顺序扩展：

1. 在 `ModelEvaluationProtocol` 增加方法映射和能力声明；
2. 在 `ModelEvaluationService` 增加对应的 manifest/geometry 适配和指标规则；
3. 为方法返回合适的 chart descriptor；
4. 只有在 descriptor 无法表达通用交互时才增加 Qt Model role；
5. 不在 QML 中复制算法计算，不新增结果文件或另一份评估入口。

FS-SAM2 的小样本流程保持独立，不接入普通测试任务的评估适配器。

## 12. 验收和静态检查

应满足以下行为：

- 测试任务下拉列表展示任务名称，多个任务的配置和 PRED 互不覆盖；
- 每次主动开始测试都重新导出、清理 PRED 并调用 Python；
- 页面选中任务才启动后台 C++ 评估，界面线程保持可响应；
- 同一进程多次切换任务命中内存缓存；
- 评估参数变化重新读取 PRED，失败时没有旧结果；
- 重新打开项目后按需重新评估且不弹窗；
- 异常检测图表只显示 GOOD/Anomaly 两个图例项和必要的竖直虚线；
- 缺失图像被跳过，缺失预测按空预测处理，并按数量打印 warning；
- 不生成评估结果持久文件。

本次静态验证：

- `cmake --build build --config Release -- /m:4` 成功；
- `git diff --check` 通过；
- 未添加或运行额外测试。
