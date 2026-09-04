# WORKLOG

> 本文件是项目唯一的任务账本。真实日志按最新在前追加在固定示例条目之后，并固定位于其他真实日志之上；`⏳ 待你裁决` 始终固定在顶部。

## ⏳ 待你裁决

<!-- 没有待裁决事项时保持本节为空。 -->

---

## 日志

<!--
建议格式：

### YYYY-MM-DD — 简短任务名

**目标**
- ...

**当前状态**
- 已完成：...
- 未完成：...

**验证证据**
- `command ...` → 关键结果
- 未验证项请明确写“未验证”

**下一步**
- ...
-->


## [示例] 修复订单导出超时

**总目标**：后台订单导出在 1 万行数据量下 30 秒内完成，不再 504。

**状态**：✅ 完成

**干到哪了**：
- [x] 定位根因：导出走了逐行 N+1 查询 —— 证据：慢日志中同款 SELECT 出现 10,412 次
- [x] 改为批量查询 + 流式写出 —— 证据：`export_test.go` 新增用例通过；本地 1 万行实测 4.2s
- [x] 隔离实例真实触发目标路径 —— 证据：staging 实测导出 12,000 行 5.1s，HTTP 200
- [x] 开关两态验证：`export_v2=off` 时回退旧路径正常

**边界**：不动导出的字段结构；不顺手重构 handler。

---

## 2026-09-04 — 完善数据集导出进度与耗时反馈

**目标**
- 让数据导出在并行处理期间分批更新进度。
- 在结束日志和 InfoBar 消息中显示耗时；开始日志不显示耗时。
- 少量数据集显示名称，大量数据集仅显示数量。

**当前状态**
- 已完成：COCO、LabelMe、Mask、Folder 导出接入并行处理进度回调，Folder 成功路径补齐 100% 进度。
- 已完成：DataManager 增加格式、数据集摘要、单数据集和批量导出的结束耗时反馈。
- 已完成：项目导出测试校验 Mask、LabelMe、COCO 的成功通知消息包含“耗时”。
- 已完成：新增 DataIO LabelMe 导出进度测试，锁定并行处理中间进度。

**验证证据**
- `cmake --build build --config Release --target dltool_data_data_ioexport_tests --parallel 4` → Release 构建通过。
- `ctest --test-dir build -C Release -R "^dltool_data_data_ioexport_tests$" --output-on-failure` → 1/1 通过。
- `cmake --build build --config Release --target dltool_model_data_export_test --parallel 4` → Release 构建通过。
- 通过 `python tools\\run_project_tests.py --skip-build` 分层调用 CTest，在同一独立项目根目录依次执行 `project-creation`、`data-creation`、`data-import`、`data-export` → 四层全部通过。
- `git diff --check` → 未发现差异格式错误。

**下一步**
- 在真实桌面环境执行一次多数据集导出，确认进度条、开始/结束日志和 InfoBar 的展示符合预期。

---

## 2026-09-04 — 调整任务账本日志顺序约束

**目标**
- 明确固定示例与真实日志的排列关系，以及账本自身维护的记录边界。

**当前状态**
- 已完成：真实日志统一放在固定示例之后。
- 已完成：真实日志按最新在前排列，最新条目固定在其他真实日志之上。
- 已完成：同步更新 `AGENTS.md` 与 `WORKLOG.md` 的顺序规则说明。
- 已完成：仅维护 `WORKLOG.md` 本身时，不为该维护动作新增日志。

**验证证据**
- 已检查 `WORKLOG.md` 当前结构，固定示例位于真实日志之前，最新真实日志位于历史真实日志之前。
- `git diff --check` → 通过。

**下一步**
- 后续新增真实日志直接插入固定示例之后，并置于其他真实日志之上。
- 仅维护 `WORKLOG.md` 时不新增自描述日志。

---

## 2026-09-04 — 清理项目打开性能诊断日志并修复树模型重复重建

**目标**
- 移除项目打开性能诊断期间新增的高频日志，避免日志 I/O 进一步拖慢 UI。
- 修复 `DataSelectionTreeModel` 因无关 `dataChanged` 角色反复重建的问题。

**当前状态**
- 已完成：树模型按实际依赖角色处理 `dataChanged`；`HasLabelsRole`、统计值等无关更新不再触发整树重建。
- 已完成：删除 `[DEBUG-open-perf]`、Qt 消息转发、缩略图请求计数及相关计时诊断代码。
- 已完成：新增 `tests/data/test_DataSelectionTreeModel.cpp` 回归测试。
- 未完成：未重新执行真实 GUI 项目打开流程，实际打开耗时未验证。

**验证证据**
- `ctest --test-dir build -C Release -R '^dltool_data_data_selection_tree_model_tests$' --output-on-failure` → 通过。
- `cmake --build build --config Release --target dltool --parallel 4` → Release 构建通过。
- `git diff --check` → 未发现差异格式错误。
- 源码检索确认已无临时性能诊断标记和 Qt 日志转发代码。

**下一步**
- 在真实 GUI 环境重新打开包含大量图像和标注的项目，确认项目打开期间不再出现连续的树模型重建及高频诊断日志。
