# 34：完成全链路验收与剩余结构清理

**What to build（交付）:** 完整工作流、安装与结构要求都有证据。

**Blocked by（阻塞依赖）:** [01：修复数据并行异常丢失](01-data-worker-errors.md)；[02：统一数据操作取消与提交结果](02-data-commit-cancel.md)；[03：关闭项目时立即拒绝新工作](03-project-close-gate.md)；[04：统一评估和聚合任务的关闭收敛](04-evaluation-shutdown.md)；[05：隔离不同任务的页脚进度](05-task-progress.md)；[06：保护覆盖导出的源文件和既有目标](06-safe-export.md)；[07：让批量导出取消与产物校验可信](07-batch-export.md)；[08：校验数据库完整 schema 契约](08-schema-validation.md)；[09：让导入后台事务覆盖全部修改](09-atomic-import.md)；[10：让数据复制移动划分原子完成](10-atomic-data-operations.md)；[11：统一格式转换的几何语义](11-geometry-formats.md)；[12：修复模型创建复制的恢复窗口](12-model-create-recovery.md)；[13：完成模型重命名删除的中断恢复](13-model-mutate-recovery.md)；[14：让模型复制与恢复不阻塞界面](14-async-model-storage.md)；[15：让测试任务目录操作可恢复](15-test-task-recovery.md)；[16：严格验证 C++ Python 任务协议](16-strict-task-protocol.md)；[17：让成功终态等待真实进程与产物](17-real-task-terminal.md)；[18：持久化训练与内部子任务的运行记录](18-train-state-storage.md)；[19：持久化测试任务状态和首评应用事实](19-test-state-storage.md)；[20：完整发布新预测隔离失败产物](20-prediction-publish.md)；[21：修正评估快照获取与失效范围](21-evaluation-snapshot.md)；[22：统一旧预测的坐标解释](22-prediction-geometry.md)；[23：限制跨任务缓存与视觉请求总量](23-visual-cache-budget.md)；[24：完成阈值搜索与图表的行为验收](24-threshold-charts.md)；[25：让设置保存失败可见且可重试](25-settings-save.md)；[26：消除参数控件刷新的写入副作用](26-parameter-edit.md)；[27：核对 Ultralytics 参数到实际执行链](27-ultralytics-parameters.md)；[28：核对异常检测及内部模型参数执行链](28-anomaly-parameters.md)；[29：让智能标注推理异步且可安全关闭](29-async-smart-annotation.md)；[30：让聚类写回使用固定输入并准确结束](30-cluster-writeback.md)；[31：让数据树增量刷新并保留选择](31-tree-projection.md)；[32：让测试环境与构建选项真正可配置](32-portable-tests.md)；[33：完成独立安装与运行包验证](33-install-runtime.md)

**Status:** ready-for-agent

此标签表示任务描述可领取；有阻塞依赖时须先完成前置。执行状态和验证证据仅维护在项目任务账本。

## 验收条件

- [ ] 全部前置票有相关 Release/CTest 证据，复核任务依赖和未验证项。
- [ ] 普通、Model/QML、项目级缺前置负向及真实 PatchCore full 流程通过，产物可在软件查看。
- [ ] 确认并删除无调用历史入口、纯别名及重复核心元数据；不开展新一轮无关重构。
- [ ] 核对冷/热评估、多任务资源、关闭和恢复证据，安装烟测通过；未验证项不得宣称完成。
- [ ] 通过公开用例入口及真实下一层依赖先建立失败/约束测试，再实现；相关 Release 构建和 CTest 通过，必要真实资源验证有证据。

## 边界

只完成本票用例及其必要预重构，沿用现有模块接口。替换时删除废弃路径，不新增长期兼容双轨；不以文件行数或类数量作为拆分依据。

