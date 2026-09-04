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

## 2026-09-05 — 修复 UNC 网络路径截断及相对路径写盘问题

**目标**
- 修复数据导出/导入及项目创建时 UNC 网络共享路径（如 `\\192.168.2.87\share\...`）被 `Utils.getCleanPath` 误截断为相对路径导致在 `build/bin/` 下误写盘的问题。
- 建立端到端路径规范化与校验机制：
  1. 重构 `dltool::ui::Utils::getCleanPath`，使用 `QUrl::toLocalFile()` 与 `dltool::common::cleanPath()` 支持本地驱动器、UNC 网络路径、Linux 路径，消除硬编码切片截断；
  2. 新增 `dltool::ui::Utils::toFileUrl`，规范 QML 本地/UNC 路径转 `file:` URL，消除 `"file:///" + path` 逆向硬拼；
  3. 后端 `DataManager::exportDatasets`、`DataManager::startImportData`、`DataManager::scanImportLabelClasses` 及 `Project::isValid` 增加前置非空与绝对路径拦截校验；
  4. 修复跨卷软硬链接失效回退拷贝与目录 junction 识别问题（`tools/dependency_utils.py`）。

**当前状态**
- 已完成：
  - `src/ui/include/ui/Utils.h` 与 `src/ui/Utils.cpp` 重构 `getCleanPath` 并新增 `toFileUrl`；
  - `src/data/qml/label/LabelImage.qml` 与 `src/project/qml/project/ProjectDelegate.qml` 改用 `Utils.toFileUrl(...)`；
  - `src/project/qml/project/ProjectForm.qml` 默认目录用 `Utils.getCleanPath(...)` 规整；
  - `src/data/DataManager.cpp` 对导出输出路径做 `clean_output_dir.isEmpty()` 与 `!QFileInfo(clean_output_dir).isAbsolute()` 前置拦截，并修复 lambda 捕获规整后路径；对导入路径做存在性与绝对路径拦截；
  - `src/project/Projects.cpp` 在 `Project::isValid` 增加路径非空与绝对路径校验，`createProject` 与 `openProject` 统一使用 `clean_path`；
  - `tools/dependency_utils.py` 支持跨盘链接拷贝回退与 Python < 3.12 junction 目录识别；
  - 编写并补全针对 UI 工具类、数据导出拦截、项目路径校验的单元测试。

**验证证据**
- `ctest -C Release -R "(tst_dltool_ui|dltool_model_project_creation_test|dltool_model_data_export_test)" --output-on-failure` → 5 个相关测试全绿通过（100% passed, 0 failed）：
  - `dltool_model_project_creation_test`: PASSED (验证空路径、相对路径被拦截，UNC 路径有效)
  - `dltool_model_data_export_test`: PASSED (验证相对路径与空导出路径触发错误信号拦截)
  - `tst_dltool_ui`: PASSED (验证本地绝对路径、UNC 路径 `file://192.168.2.87/share/...` 与裸路径 `\\192.168.2.87\share\...` 规整和 `toFileUrl` 转换)
  - `dltool_model_data_creation_test` & `dltool_model_data_import_test`: PASSED

**下一步**
- 继续按需求迭代后续功能。

---

## 2026-09-04 — 补齐数据校验、特征聚类检索与小样本学习的 spdlog 日志

**目标**
- 为审计出的 3 类缺少 `spdlog` 日志记录的场景补齐终端与文件日志（共 18 处）：
  1. 数据前置校验与扫描失败（`DataManager.cpp` 共 5 处）
  2. 特征检索与聚类任务完成与失败（`SearchControllerBase.cpp` 2 处、`ImageClusterController.cpp` 4 处、`RoiClusterController.cpp` 3 处，共 9 处）
  3. 小样本学习生命周期流转（`FewShotLearningController.cpp` 共 4 处）

**当前状态**
- 已完成：在 `src/data/DataManager.cpp` 中为 `scanImportLabelClasses`（格式不支持、无法创建扫描器、异步扫描失败回调）、`copyToDatasetAsync`（标注加载拦截）、`splitDataset`（划分失败）补齐 `spdlog::error` / `spdlog::warn`。
- 已完成：在 `src/feature/SearchControllerBase.cpp` 中为 `finishSearch` 成功与失败分支补充耗时日志 `spdlog::info` / `spdlog::error`。
- 已完成：在 `src/feature/ImageClusterController.cpp` 中为 `applyClusterPlan`（失败与成功）以及 `finishCluster`（响应失败、计划构建失败）补充 `spdlog::error` / `spdlog::info`。
- 已完成：在 `src/feature/RoiClusterController.cpp` 中为 `finishCluster`（响应失败、应用结果失败、聚类完成）补充 `spdlog::error` / `spdlog::info`。
- 已完成：在 `src/feature/FewShotLearningController.cpp` 中为 `finishRun`（失败分支即使无消息也默认赋错并打日志）、`startPredictionImports`（无目标正常完成）、`startNextPredictionImport`（数据管理器未初始化错误、全部批次导入完成）补充 `spdlog::error` / `spdlog::info`。

**验证证据**
- `cmake --build build --config Release --target dltool_data dltool_feature --parallel 4` → 编译与链接成功。
- `cmake --build build --config Release --target dltool_data_dataset_splitter_tests dltool_model_data_creation_test --parallel 4` → 编译成功。
- `ctest -C Release -R "(dltool_data_dataset_splitter_tests|dltool_model_data_creation_test)" --output-on-failure` → 3/3 测试全部通过（0 失败）。

**下一步**
- 在日志输出监控中持续关注特征检索与小样本任务运行全流程日志流转。

---

## 2026-09-04 — 彻底禁用图像删除、移动与复制的进度对话框

**目标**
- 图像的删除、移动与复制耗时极短，彻底禁用进度对话框（不再使用数量阈值判定），避免任何弹窗闪烁。

**当前状态**
- 已完成：在 `DataManager.cpp` 中将 `deleteSelectedImages`、`copyToDatasetAsync`、`moveToDatasetAsync` 的 `options.manage_progress` 统一设置为 `false`。
- 已完成：删除无用的数量阈值常量 `kMinProgressBatchImageCount`。
- 已完成：更新 `test_DataCreation.cpp` 中的 `progressManagerNotTriggeredForLightweightOperations` 测试用例，验证 60 张等较大批量复制与移动以及删除操作均不触发 ProgressManager。

**验证证据**
- `cmake --build build --config Release --target dltool_data --parallel 4` → 编译成功。
- `cmake --build build --config Release --target dltool_model_data_creation_test --parallel 4` → 编译成功。
- `ctest -C Release -R dltool_model_data_creation_test --output-on-failure` → 2/2 测试通过。

**下一步**
- 在 GUI 界面验证图像批量操作无弹窗打扰。

---

## 2026-09-04 — 移除轻量及前置校验失败场景下的虚假进度对话框触发

**目标**
- 消除 5 类不合理的 `ProgressManager` 进度对话框触发，避免闪烁或虚假任务启动：
  1. 导入数据时的并发/忙状态拦截
  2. 导入数据时的前置数据库校验与格式解析失败
  3. 添加数据集 (`addDataset`)
  4. 更新/重命名数据集 (`updateDataset`)
  5. 单张/极少量图像的删除、移动与复制 (`deleteSelectedImages`, `copyToDatasetAsync`, `moveToDatasetAsync`)

**当前状态**
- 已完成：在 `DataManager.cpp` 中移除 `startImportData` 并发/忙状态拦截分支中的 `startTask`、`addMessage` 与 `completeTask`。
- 已完成：将 `startImportData` 的 `startTask` 挪移至数据库完整性校验与导入器创建成功之后，清理前置失败分支中的 `completeTask` / `addMessage`。
- 已完成：在 `addDataset` 与 `updateDataset` 中设置 `options.manage_progress = false`。
- 已完成：定义 `kMinProgressBatchImageCount = 50`，在 `deleteSelectedImages`、`copyToDatasetAsync`、`moveToDatasetAsync` 中仅在图像数量 $\ge 50$ 时启用 `manage_progress` 对话框。
- 已完成：在 `test_DataCreation.cpp` 中新增 `progressManagerNotTriggeredForLightweightOperations` 测试用例，覆盖上述轻量操作、格式不支持拦截、并发忙拦截及小批量图像移动/复制。

**验证证据**
- `cmake --build build --config Release --target dltool_data --parallel 4` → 编译成功。
- `cmake --build build --config Release --target dltool_model_data_creation_test --parallel 4` → 编译成功。
- `ctest -C Release -R dltool_model_data_creation_test --output-on-failure` → 2/2 测试通过（包含 `createsNamedEmptyDataset` 与新增的 `progressManagerNotTriggeredForLightweightOperations`）。
- `ctest -C Release -R dltool_model_data_import_test --output-on-failure` → 3/3 测试通过，保证导入基础功能不受影响。

**下一步**
- 在 GUI 界面测试单图删除/移动及数据集增改，观察界面是否干净无弹窗闪烁。

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
