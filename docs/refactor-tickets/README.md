# 架构收敛开发任务

依据 [实施规格](../REFACTOR_SPEC.md) 和 [最终方案](../../final_plan.md) 拆为独立可验证的完整用例。每个任务单独成文，相关层次的实现、测试及必要预重构在同一票内交付。

## 使用规则

- 从阻塞依赖全部完成的任务中领取；编号是拓扑顺序，不要求无依赖任务串行执行。
- 每票的 Blocked by 是依赖唯一来源；本索引只提供导航，不维护另一份依赖或进度表。
- ready-for-agent 表示规格具备执行条件，不表示依赖完成、代码实现完成或测试已通过。
- 执行进度、验证结果及未完成项只记录在 [WORKLOG](../../WORKLOG.md)，任务文件的验收清单作为规格保留。
- 先在目标 seam 补最小失败/约束测试，随后实现并清理旧路径。相关 Release/CTest 每票验证，全量验收留到最后一票。
- 若跨票修改同一文件，协调工作区即可；不人为增加不存在的业务阻塞。

## 任务索引

1. [修复数据并行异常丢失](01-data-worker-errors.md)
2. [统一数据操作取消与提交结果](02-data-commit-cancel.md)
3. [关闭项目时立即拒绝新工作](03-project-close-gate.md)
4. [统一评估和聚合任务的关闭收敛](04-evaluation-shutdown.md)
5. [隔离不同任务的页脚进度](05-task-progress.md)
6. [保护覆盖导出的源文件和既有目标](06-safe-export.md)
7. [让批量导出取消与产物校验可信](07-batch-export.md)
8. [校验数据库完整 schema 契约](08-schema-validation.md)
9. [让导入后台事务覆盖全部修改](09-atomic-import.md)
10. [让数据复制移动划分原子完成](10-atomic-data-operations.md)
11. [统一格式转换的几何语义](11-geometry-formats.md)
12. [修复模型创建复制的恢复窗口](12-model-create-recovery.md)
13. [完成模型重命名删除的中断恢复](13-model-mutate-recovery.md)
14. [让模型复制与恢复不阻塞界面](14-async-model-storage.md)
15. [让测试任务目录操作可恢复](15-test-task-recovery.md)
16. [严格验证 C++ Python 任务协议](16-strict-task-protocol.md)
17. [让成功终态等待真实进程与产物](17-real-task-terminal.md)
18. [持久化训练与内部子任务的运行记录](18-train-state-storage.md)
19. [持久化测试任务状态和首评应用事实](19-test-state-storage.md)
20. [完整发布新预测隔离失败产物](20-prediction-publish.md)
21. [修正评估快照获取与失效范围](21-evaluation-snapshot.md)
22. [统一旧预测的坐标解释](22-prediction-geometry.md)
23. [限制跨任务缓存与视觉请求总量](23-visual-cache-budget.md)
24. [完成阈值搜索与图表的行为验收](24-threshold-charts.md)
25. [让设置保存失败可见且可重试](25-settings-save.md)
26. [消除参数控件刷新的写入副作用](26-parameter-edit.md)
27. [核对 Ultralytics 参数到实际执行链](27-ultralytics-parameters.md)
28. [核对异常检测及内部模型参数执行链](28-anomaly-parameters.md)
29. [让智能标注推理异步且可安全关闭](29-async-smart-annotation.md)
30. [让聚类写回使用固定输入并准确结束](30-cluster-writeback.md)
31. [让数据树增量刷新并保留选择](31-tree-projection.md)
32. [让测试环境与构建选项真正可配置](32-portable-tests.md)
33. [完成独立安装与运行包验证](33-install-runtime.md)
34. [完成全链路验收与剩余结构清理](34-full-acceptance.md)

