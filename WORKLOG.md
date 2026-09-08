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

## 2026-09-08 — 修复模型创建复制的恢复窗口

**目标**
- 交付 Ticket 12：修复模型创建复制的恢复窗口（创建复制中断后仍能恢复一致模型）。
- 数据库已提交、日志 ID 未更新时中断，可用预先持久化 UUID 找回记录。
- 清理失败保留恢复凭证，重复恢复不丢数据、不创建重复模型。
- 使用已有存储 Adapter 注入失败，重开核对目录、记录与复制范围。
- 遵循 TDD，先增加失败/约束测试，再实现功能，并通过 Release 构建与 CTest。

**当前状态**
- 已完成：在 `IModelRecordStore` 接口中增加 `findModelByUuid` 方法并在 `ProjectModelRecordStore` 与测试 mock `MemoryModelRecordStore` 中实现。
- 已完成：在 `tests/model/test_ModelLifecycle.cpp` 建立 4 组 TDD 测试：
  1. `recoversModelByPersistedUuidWhenJournalModelIdNotUpdated`：模拟数据库提交后、日志记录 model_id 尚未回写即中断的场景，验证恢复过程成功通过预先持久化的 UUID 找回记录并继续完成模型目录落地与日志清理。
  2. `retainsJournalWhenCleanupFailsAndRepeatedRecoveryDoesNotDuplicateOrLoseData`：模拟目标模型已存在但暂存区清理失败的场景，验证恢复操作保留日志凭证、重复调用 `recoverPending()` 不丢失数据、不生成重复记录，故障解除后能安全收尾。
  3. `recoversInterruptedModelCopyAndVerifiesDirectoryRecordAndCopyScope`：注入发布失败并执行恢复，核对目标目录存在且无残留操作日志，核对 UUID/名称/架构等数据库记录一致，核对模型训练参数、数据集选择配置与权重文件均完整复制。
  4. `recoversInterruptedModelCopyWithNoWeightsAndVerifiesCopyScope`：验证不复制权重时的复制与中断恢复行为，核对参数与数据集完整复制，且目标模型权重目录中绝不生成非预期的权重文件。
- 已完成：重构 `src/model/ModelLifecycle.cpp` 的 `recoverPending()` 逻辑，支持未绑定 model_id 时通过 uuid 兜底找回模型记录，并在 Create/Copy 恢复流程中严格校验暂存区与目标目录清理操作的返回值，确保清理失败时不误删日志凭证。
- 未完成：无。

**验证证据**
- `cmake --build build --config Release --target dltool_model_storage_params_tests` → 构建成功，零编译警告/错误。
- `ctest --test-dir build --output-on-failure -C Release -R "dltool_model_storage_params_tests"` → 100% 测试通过（包含 ModelLifecycleTest 全部 6 个测试用例：正常生命周期、注入失败回滚、UUID 兜底找回、清理失败保留凭证、复制中断恢复核对全范围、不含权重复制中断恢复）。
- `ctest --test-dir build --output-on-failure -C Release -R "dltool_model_(evaluation|dataset|tasks|storage_params)_tests"` → 4/4 核心测试全部 Passed。

**下一步**
- 开始 Ticket 13：`docs/refactor-tickets/13-model-eval-config-sync.md`，收敛模型评估配置同步逻辑。

## 2026-09-08 — 统一格式转换的几何语义

**目标**
- 交付 Ticket 11：统一格式转换的几何语义（不同导入导出格式的区域位置与合法性一致）。
- 固定坐标验证 rectangle、polygon、Mask、边缘裁剪及像素取整。
- 有效小区域不按面积擅自丢弃，不足三个轮廓点按现有规则处理。
- 复用几何内核；带孔等格式能力明确验证，不承诺不可表达的无损转换。
- 遵循 TDD，先增加几何内核约束用例与导入导出数据格式测试，再实现并保证 CTest 通过。

**当前状态**
- 已完成：扩展 `tests/common/test_GeometryKernel.cpp`，补充坐标退化校验、边缘裁剪保留有效微小面积、精细包围盒计算、单像素/双像素轮廓退化兜底、带孔 Mask 轮廓提取与外扩光栅化特性验证、多边形光栅化像素级测试。
- 已完成：在 `tests/data/test_DataGeometryFormats.cpp` 建立端到端 TDD 测试套件，覆盖 COCO 分割多边形导入及边界裁剪、COCO 分割项目中无 segmentation 的 bbox 转换为 4 点多边形导入导出、COCO 目标检测项目中 bbox 保持原样不生多边形、LabelMe 矩形与多边形按项目类型转换与裁剪、Mask 掩码非等比尺寸缩放坐标映射。
- 已完成：重构 `src/data/DataIO.cpp` 中的 `COCOIO::doImport` 与 `COCOIO::doExport`，在分割与异常检测项目中统一使用 `dltool::common::geometry::rectangleToPolygon` 将 bbox 规范化为 4 点多边形，对齐 LabelMe 与 Mask 语义；重构 `MaskIO::readMaskGeometry` 与 `src/common/MaskPolygonUtils.cpp` 中的 fallback 多边形生成统一复用几何内核。
- 已完成：修复 `test_LabelMeIO.cpp` 异步 worker 线程未等待结束导致的局部析构竞态。
- 未完成：无。

**验证证据**
- `cmake --build build --config Release --target dltool_data_data_geometry_formats_tests dltool_common_geometry_tests` → 编译成功 0 error。
- `ctest --test-dir build --output-on-failure -C Release -R "(data_geometry_formats|geometry_tests)"` → 2/2 tests passed (100% passed, 0 failed).
- `ctest --test-dir build --output-on-failure -C Release -R "(data_io|label_me|roundtrip)"` → 8/8 tests passed (100% passed, 0 failed).

**下一步**
- 继续推进 Ticket 12：`docs/refactor-tickets/12-model-create-recovery.md`（模型创建中断与脏目录恢复）。

## 2026-09-08 — 让数据复制移动划分原子完成

**目标**
- 交付 Ticket 10：图片与关联记录一起提交或回滚，操作后可正确浏览。
- 复制、移动、划分使用冻结选择和单库原子批量接口。
- 关联记录写入中途失败不留半成品，不忽略补偿错误。
- 提交后取消与重新打开均保持图片、标签、标注、数据集和展示一致。
- 遵循 TDD，先增加约束和回滚集成测试，再实现单库原子方法，并删除旧的逐表写入与手动补偿逻辑。

**当前状态**
- 已完成：在 `ProjectDataBase` 接口中定义 `LabelSnapshot`、`ImageSnapshot`、`DatasetSplitTarget`、`AtomicCopyOutput`、`AtomicSplitOutput`，以及单库单事务原子操作 `copyImagesAtomic`、`splitDatasetAtomic`、`moveImagesAtomic`。
- 已完成：在 `tests/project/test_DataSplit.cpp` 中新增 TDD 约束与集成测试：`copiesImagesWithLabelsAndTagsAtomically`、`movesImagesAtomicallyBetweenDatasets`、`atomicRollbackLeavesNoLeftoverRecords`，验证原子提交、直接重开核对一致性，以及中途取消时数据库 0 记录残留。
- 已完成：在 `DataBase.cpp` 中实现 `copyImagesAtomic`、`splitDatasetAtomic`、`moveImagesAtomic`，在单一 `sqlpp::transaction_t` 中原子写入图片、extraData、标签、标注及关联 Tag，内置协作式取消检查，失败时全量回滚且不忽略任何错误。
- 已完成：重构 `DataManager::copyToDatasetAsync`、`splitDataset`、`moveToDatasetAsync`，统一使用冻结快照与单库原子方法，彻底删除多表分步写库与 `deleteImages(..., ignored_error)` / `deleteDatasetsWithContents` 等脆弱的手工补偿代码。
- 已完成：修复数据集名称生成中带括号非法字符的问题（格式统一为 `_` 命名后缀）。
- 已完成：通过全部相关 Release 编译与 CTest 集成测试。

**验证证据**
- `ctest --test-dir build -C Release -R dltool_model_data_split_test -V` → 6 项测试全部通过（含 `createsStratifiedDatasetCopies`、`copiesImagesWithLabelsAndTagsAtomically`、`movesImagesAtomicallyBetweenDatasets`、`atomicRollbackLeavesNoLeftoverRecords`，耗时 0.50s）。
- `ctest --test-dir build -C Release -R "(dltool_model_data_split_test|dltool_model_data_creation_test|dltool_model_data_import_test|dltool_model_data_export_test|dltool_model_data_roundtrip_test)" --output-on-failure` → 全部 5 项数据集成测试通过。
- `ctest --test-dir build -C Release -R database --output-on-failure` → 数据库完整性与 Schema 校验测试通过。

**下一步**
- 检查并执行 Ticket 11 相关任务。

## 2026-09-08 — 让导入后台事务覆盖全部修改

**目标**
- 导入失败恢复全部修改，写库不阻塞界面。
- worker 自有 SQLite 连接执行单事务，GUI 线程只在提交后应用已提交结果，消除 `BlockingQueuedConnection` 导致的界面卡顿。
- 取消或失败时，通过 SQLite 事务引擎级回滚完整恢复已有类别属性（如分组更改）与新增图片、标签、标注；成功时精确核对固定夹具。
- 遵循 TDD，先增加失败/约束集成测试用例，再实现并删除旧的脆弱逐项补偿与分批代码；确保 Release 构建与 CTest 全部通过。

**当前状态**
- 已完成：在 `tests/project/test_DataImport.cpp` 中新增 `cancelledImportRollsBackCompletedBatches` 和 `cancelledImportRestoresModifiedExistingClassAttributes` 用例，通过真实项目库与夹具复现取消时对已有类别属性修改的补偿遗漏，并建立约束。
- 已完成：设计并实现 `ImportDatabaseWriter`（采用 PIMPL 模式完全隔离 SQLite/sqlpp 依赖），直接在 worker 线程以 `Qt::DirectConnection` 接收导入批次，使用 worker 独立 SQLite 连接与 `sqlpp::transaction_t` 在单事务内完成图片、标注、类别及图像级 extra_data 写入。
- 已完成：重构 `DataManager`，移除脆弱的 `writeImportBatch`、`rollbackPendingImport`、`finishBatchedImport` 及 `PendingImportTask`，GUI 线程在导入过程中不碰模型数据；在 worker 发送 `finished` 信号后，GUI 仅在提交成功时统一调用 `reloadFromDatabase()` 刷新。
- 已完成：修复 `DataManager::addDataset` / `updateDataset` 异步完成回调中的时序问题，保证内存模型更新完成后才置 `dataOperationRunning = false`。
- 已完成：清理无用的长短期双轨逻辑，确保 Release 构建与 CTest 通过。

**验证证据**
- `cmake --build build --config Release --target dltool_data dltool_model_data_creation_test dltool_model_data_import_test dltool_data_data_io_tests dltool_model_data_export_test dltool_model_data_roundtrip_test` → 构建全部成功，0 错误 0 警告。
- `ctest --test-dir build -C Release -R "^dltool_model_data_import_test$" -V` → 5 个测试用例全部通过（100% passed, 0 failed）。
- `ctest --test-dir build -C Release -R "^dltool_model_data_creation_test$" -V` → 全部通过（100% passed, 0 failed）。
- `ctest --test-dir build -C Release -R "^dltool_data_data_io_tests$" -V` → 全部通过（100% passed, 0 failed）。
- `ctest --test-dir build -C Release -R "^dltool_model_data_export_test$" -V` → 全部通过（100% passed, 0 failed）。
- `ctest --test-dir build -C Release -R "^dltool_model_data_roundtrip_test$" -V` → 全部通过（100% passed, 0 failed）。

**下一步**
- 继续推进 Ticket 10 (`docs/refactor-tickets/10-safe-export.md`：规范导出目录暂存与覆盖防护)。

## 2026-09-08 — 校验数据库完整 schema 契约与迁移回滚

**目标**
- 从 canonical DDL（SQL 资源正本）直接派生数据库契约，自动比对列、外键约束、唯一约束、必要索引和表类型（严格识别 table，拒绝 view 等非表对象）。
- 确保空库及已支持版本（user_version == kCurrentSchemaVersion）正常可用，未知版本明确拒绝，不新增通用兼容平台。
- 在模式升级/迁移事务中注入失败时，数据库结构、数据行以及 user_version 完整回滚，并通过真实数据库接口（ProjectDataBase / ModelDataBase）验证。

**当前状态**
- 已完成：在 `src/database/DatabaseSchema.cpp` 中重构 schema 契约生成机制，通过内存临时数据库加载 canonical DDL 资源（`resourcesFor` 及 settings 模板）并自动解析 `PRAGMA table_info`、`PRAGMA foreign_key_list`、`PRAGMA index_list`/`index_info` 和 `sqlite_master.type`，派生出完整的 `TableSpec`，彻底消除手动维护 C++ 表结构的脆弱性与不一致风险。
- 已完成：在 `DatabaseSchema.cpp::validateTable` 中强化五维校验：严格要求 `type == 'table'`（拒绝视图）、列名与类型及主键/非空标志严格核对、外键约束匹配（源列、目标表、目标列）、唯一约束（`UNIQUE` 约束及唯一索引覆盖列集）与索引匹配。
- 已完成：将 `DatabaseSchema.h` 导出至 `src/database/include/database/DatabaseSchema.h` 并暴露 `DATABASE_API void setMigrationHookForTest(MigrationHook hook)`，支持在迁移事务执行过程中注入测试故障。
- 已完成：在 `DatabaseSchema.cpp::ensureSchema` 的事务内执行建表、升级与校验，若迁移 hook 失败或校验未通过，立即执行 `ROLLBACK`，确保结构变动、数据更新与 `PRAGMA user_version` 完整回滚。
- 已完成：在 `tests/database/test_DatabaseSchema.cpp` 中通过 TDD 新增 6 个测试用例，覆盖：视图替代真实表拒绝、缺失外键约束拒绝、缺失唯一约束拒绝（包括普通表与动态 settings 表）、通过 `ProjectDataBase::openProject` 验证迁移失败后结构与数据和版本完整回滚、通过 `ModelDataBase::readTrainParams` 验证模型库迁移失败回滚。

**验证证据**
- `ctest --test-dir build -C Release -R "^dltool_database_database_schema_tests$" -V` → 18/18 全部 Passed (0.20 sec)，覆盖表类型、外键、唯一约束及 `ProjectDataBase`/`ModelDataBase` 真实回滚。
- `ctest --test-dir build -C Release -R "^dltool_model_data_export_test$" --output-on-failure` → 4/4 全部 Passed (2.07 sec)，包含项目创建与全部模型/数据集成测试无回归。

**下一步**
- 提交本阶段代码：`refactor: 校验数据库完整 schema 契约与迁移回滚`。
- 推进 Ticket 09：`09-atomic-import.md`。

---

## 2026-09-08 — 规范批量导出取消作用域与产物清单校验

**目标**
- 解决批量导出在取消后仍继续启动后续数据集，无法真正停止整批任务的问题。
- 确保导出目标目录存在旧文件时，未执行项绝不能计入成功，目标旧文件完整保留。
- 统一 Folder、Mask、LabelMe、COCO 在取消时的结果契约：立即退出、报告“导出已取消”、丢弃暂存产物、不触碰目标目录已有文件。
- 批次进度按数据集数量分区平滑映射（setProgressRange），开始日志记录数据集清单与格式，结束耗时与实际成功/失败/未执行统计严格一致。

**当前状态**
- 已完成：在 `DataIO` 中增加 `setProgressRange(min, max)` 并在 `updateProgress` 中进行区间映射，避免批量导出进度条在各数据集之间反复跳回 0；规范四大导出器（FolderIO、MaskIO、LabelMeIO、COCOIO）取消契约，在前后 worker 边界严格拦截并统一发送 `exportFinished(false, "导出已取消")`。
- 已完成：在 `DataIO::validateExportOutput` 中增强清单校验，对于 COCO 严格核对 `instances.json` 中图像清单对应的物理文件存在性与大小，对于 Folder/Mask/LabelMe 严格核对清单完整性。
- 已完成：在 `DataManager` 中引入 `active_export_cancel_token_`；在 `requestDataOperationCancel` 中触发标记；在 `exportDatasets` 的数据准备阶段与数据集遍历循环中增加协作取消检测，取消后立即终止整批任务，向用户发送警告通知并记录未执行数据集数量。
- 已完成：在 `tests/data/test_DataIOExport.cpp` 中编写 TDD 测试 `exportersUseConsistentContractOnCancellation` 与 `manifestValidationVerifiesFileExistenceAndContent`；在 `tests/project/test_DataExport.cpp` 中编写真实项目环境集成测试 `batchExportCancellationStopsSubsequentDatasetsAndPreservesTarget`。

**验证证据**
- `ctest --test-dir build -C Release -R "^dltool_data_data_ioexport_tests$" --output-on-failure` → 9/9 全部 Passed (0.62 sec)。
- `ctest --test-dir build -C Release -R "^dltool_model_data_export_test$" --output-on-failure` → 4/4 全部 Passed (3.29 sec)，覆盖双数据集批量取消、未执行项不计入成功、旧产物完整性保护。

**下一步**
- 提交本阶段代码：`refactor: 规范批量导出取消作用域与产物清单校验`。
- 推进 Ticket 08。

---

## 2026-09-08 — 覆盖导出暂存校验与源文件防覆盖保护

**目标**
- 解决数据导出（COCO、LabelMe、Mask、Folder）时，若导出路径与源图像同名/为路径别名或导出目录包含源图像，会删除/破坏源数据的问题。
- 解决导出中途失败或取消时，未完成的文件散落在目标目录中污染已有文件，甚至清除目标目录中已有用户文件的问题。
- 严格校验导出目标路径：禁止将 Windows UNC 路径解析为应用工作目录相对路径；禁止无显式根目录的相对路径导出。
- 保证导出采用原子暂存发布（SafeExportScope）：在暂存区完成全部导出产物及 manifest 校验后，才原子替换发布到目标目录；若导出失败则回滚并保留目标目录原有内容。

**当前状态**
- 已完成：在 `DatasetIO` 中增加 `isSameFileOrAlias`、`isPathInsideDirectory`、`resolveExportPath`，并加固 `copyFile`，拒绝覆盖同一文件/别名。
- 已完成：在 `Utils` 中增加 `isUncPath` 和 `isValidUncPath`，在 `runtimePath` 和 `resolvePath` 中禁止将 UNC 路径当成相对路径拼接到工作目录。
- 已完成：在 `DataIO` 中实现 `SafeExportScope`，提供 `.staging_<name>_<uuid>` 隔离暂存与 `.backup_<name>_<uuid>` 失败回滚机制；实现 `checkExportSourceCollision`，在导出前预先拦截目标目录或目标文件与源图像冲突。
- 已完成：更新 `COCOIO`、`LabelMeIO`、`MaskIO`、`FolderIO` 四种导出器：前置冲突检查 -> 写入隔离暂存区 -> `validateExportOutput` 校验产物清单 -> `scope.publish()` 原子发布。
- 已完成：更新 `DataManager::exportDatasets`，使用 `resolveExportPath` 统一校验输出路径并前置拦截文件冲突。
- 已完成：编写 `test_DataIOExport.cpp`，覆盖源文件碰撞防破坏保护、导出失败保留目标目录已有文件、原子暂存发布以及 UNC/相对路径语义校验 4 个新用例。

**验证证据**
- `ctest --test-dir build -C Release -R "^dltool_data_data_ioexport_tests$" --output-on-failure` → 7/7 全部 Passed (0.34 sec)。
- `ctest --test-dir build -C Release -R "^dltool_model_data_export_test$" --output-on-failure` → 4/4 全部 Passed (3.04 sec)。

**下一步**
- 提交本阶段代码：`refactor: 覆盖导出暂存校验与源文件防覆盖保护`。
- 推进 Ticket 07：批量导出取消作用域与清单校验。

---

## 2026-09-08 — 隔离不同任务的页脚进度与终态

**目标**
- 解决后台任务（数据导入/导出、特征聚类/检索等）复用全局单例 `ProgressManager` 时没有任务标识隔离，导致并发或乱序任务的进度、消息相互覆盖、失败或取消被误报为 100% 完成、多次重复通知的问题。
- 确保前台状态展示真实反映当前活跃任务，旧任务的迟到进度/消息被丢弃，项目关闭时重置活跃状态。

**当前状态**
- 已完成：在 `ProgressManager` 中引入 `activeTaskId` 和任务身份校验；`startTask` 记录当前活跃任务 ID；`updateProgress`、`addMessage`、`completeTask`、`finishTask` 严格按任务 ID 过滤，防止不同任务交织覆盖。
- 已完成：`finishTask(taskId, success)` 规范终态行为：仅在 `success=true` 时置进度为 100%，失败或取消保留当前真实进度；添加 `if (!is_running_) return;` 避免重复终态通知。
- 已完成：在 `DataOperationWorkflow`、`DataManager`、`DataIO`、`SearchControllerBase`、`RoiClusterController`、`ImageClusterController` 中接入任务标识，并将失败/取消的真实成功状态传入 `finishTask`。
- 已完成：在 `Project::shutdown()` 中调用 `ProgressManager::reset()`，保证项目关闭时清空活跃任务状态与消息。
- 已完成：编写 `ProgressManagerTest` 6 个单元测试用例，覆盖任务身份隔离、交叉任务过滤、失败/取消不伪装100%、重复终态幂等、reset 清理和消息过滤。

**验证证据**
- `ctest --test-dir build -C Release -R "^(tst_dltool_ui|dltool_data_data_operation_workflow_tests|dltool_feature_lifecycle_tests|dltool_model_data_creation_test|dltool_model_project_shutdown_test)$"` → 6/6 全部 Passed (1.98 sec)。
- `dltool_model_project_creation_test` 重新验证通过。

**下一步**
- 提交本阶段代码：`refactor: 隔离不同任务的页脚进度与终态`。
- 推进 Ticket 06：安全导出与路径校验。

---

## 2026-09-08 — 统一模型评估与聚合任务的关闭收敛与快速取消

**目标**
- 解决测试任务管理器在关闭时，对缓存的评估任务逐个调用 shutdown 并在首个任务上等待共享线程池，导致后续并发评估未被及时取消而死锁/超时的问题。
- 解决视图模型快速切换筛选时，旧的聚合计算未被真正中断而持续累积占用后台线程的问题。
- 保证多评估任务与筛选聚合能够协同取消、安全结束、不发布迟到结果且不强杀线程。

**当前状态**
- 已完成：在 `ModelTestTaskManager::shutdownCachedEvaluations` 中改为两阶段收敛：第一阶段向所有缓存评估及当前评估发送 `beginShutdown()` 广播取消；第二阶段由共享线程池 `evaluation_pool_` 统一 `waitForDone()`；第三阶段再由各执行者完成析构收尾。
- 已完成：在 `ModelEvaluationViewModel` 中实现 `beginShutdown()` 和 `activeAggregationCancelToken()`，并引入 `aggregation_cancel_token_`；当切换筛选或调度新聚合时立即取消上一轮未完成的聚合 worker。
- 已完成：在 `aggregateEvaluation` 中引入 `cancel_token` 协作取消机制，在实例、图像、混淆矩阵、阈值搜索及图表构建阶段检查取消并提前终止。
- 已完成：在 `test_AggregateEvaluation.cpp` 中新增 `cancellationAbortsAggregateEvaluationEarly` 测试；在 `test_ModelEvaluationViewModel.cpp` 中新增 `rapidFilterChangesCancelActiveAggregationWork` 测试；在 `test_ModelTestTaskManager.cpp` 中新增 `shutdownWithMultipleControlledEvaluationsCancelsAllExecutorsBeforeWaiting` 测试。

**验证证据**
- `cmake --build build --config Release --target dltool_model_evaluation_tests dltool_model_tasks_tests --parallel 4` → 构建通过。
- `ctest --test-dir build -C Release -R '^dltool_model_evaluation_tests$' -V` → 10/10 全部通过（包含聚合取消与快速切换筛选测试）。
- `ctest --test-dir build -C Release -R '^dltool_model_tasks_tests$' -V` → 全部通过（包含受控双评估并发运行并关闭测试）。

**下一步**
- 提交本阶段代码：`refactor: 统一模型评估与聚合任务的关闭收敛与快速取消`。
- 推进 Ticket 05：收敛模型操作恢复及异步生命周期。

---

## 2026-09-08 — 项目关闭等待期间拒绝新数据写入

**目标**
- 解决项目在关闭流程中，下游 Feature/Model 控制器等待耗时任务退出期间，排队的数据完成回调可能向数据库发起新写入（如 ensureDataset）污染数据的问题。
- 确保关闭闸门在项目关闭启动瞬间立即生效，关闭过程幂等且彻底。

**当前状态**
- 已完成：在 `DataManager` 中提供 `beginShutdown()`，在 `Project::shutdown()` 进入下游等待前立即调用，提前设置 `shutting_down_ = true` 并向数据操作广播取消。
- 已完成：`DataManager::shutdown()` 引入 `cleaned_up_` 标志保障清理过程幂等。
- 已完成：在 `test_ProjectShutdown.cpp` 中新增集成测试 `rejectsNewDataWritesAfterProjectShutdownBegins` 和 `repeatedCloseAndSwitchProjectLeavesCleanState`，验证关闭等待期间写入被拒、关闭后数据库未被污染、反复关闭/切换项目状态干净。

**验证证据**
- `cmake --build build --config Release --target dltool_model_project_shutdown_test --parallel 4` → 构建通过。
- `ctest --test-dir build -C Release -R '^dltool_model_project_shutdown_test$' -V` → 5/5 全部通过。

**下一步**
- 提交本阶段代码：`refactor: 项目关闭等待期间拒绝新数据写入`。
- 推进 Ticket 04：收敛模型评估与聚合任务的取消与关闭。

---

## 2026-09-08 — 统一数据操作提交后取消保留已提交结果

**目标**
- 解决数据操作工作流（DataOperationWorkflow）在 worker 提交成功后被请求取消时，终态结果被错误置为 cancelled/failure 的问题。
- 保证已成功提交的数据操作状态不可逆，防止界面与数据库状态产生分歧。

**当前状态**
- 已完成：在 `DataOperationWorkflow` 中收敛取消逻辑：当 `result.success == true` 时，迟到的取消请求不再覆盖成功状态，`result.cancelled` 仅在 `!result.success` 时成立。
- 已完成：在 `test_DataOperationWorkflow.cpp` 中新增测试 `cancellationAfterSuccessPreservesCommittedResult`，验证 worker 提交成功后触发的取消请求仍保留 `success = true` 与 `cancelled = false`。

**验证证据**
- `cmake --build build --config Release --target dltool_data_data_operation_workflow_tests --parallel 4` → 构建通过。
- `ctest --test-dir build -C Release -R '^dltool_data_data_operation_workflow_tests$' -V` → 7/7 全部通过。

**下一步**
- 提交本阶段代码：`refactor: 统一数据操作提交后取消保留已提交结果`。
- 推进 Ticket 03：项目关闭等待期间拒绝新数据写入。

---

## 2026-09-08 — 等待数据并行全部 worker 退出后再裁决异常

**目标**
- 解决数据导入/导出并行执行中，因异常检查早于 worker join 导致末尾 worker 异常被吞掉的问题。
- 让 DataIO 在后台 worker 抛出异常时准确发布失败终态并结束 Busy，失败只发布一次。

**当前状态**
- 已完成：`parallelFor` 统一等待所有 worker 线程 join 后再检查并 rethrow 捕获的第一个异常。
- 已完成：`DataIO::runInThread` 支持传入 `on_failure` 回调，捕获异常时自动触发格式对应的 `importFinished` / `exportFinished` / `labelClassesScanned` 失败发布。
- 已完成：在 `test_DataIO.cpp` 中新增可控并发异常测试 `parallelWorkerExceptionPropagatesAndPublishesFailureOnce`，验证最后一个 worker 异常被捕获且失败只发布一次。

**验证证据**
- `cmake --build build --config Release --target dltool_data_data_io_tests --parallel 4` → Release 构建通过。
- `ctest --test-dir build -C Release -R '^dltool_data_data_io_tests$' -V` → 4/4 测试通过（含新增异常测试）。
- `git diff --check -- src/data/include/data/DataIO.h src/data/DataIO.cpp tests/data/test_DataIO.cpp` → 校验通过。

**下一步**
- 提交本阶段代码：`refactor: 等待数据并行全部 worker 退出后再裁决异常`。
- 继续推进 Ticket 02：统一数据操作提交后取消保留已提交结果。

---

## 2026-09-08 — 收敛小样本学习子任务终态

**目标**
- 让 FS-SAM2 取消时创建的 Pending 子任务也进入可观察终态。
- 让小样本学习控制器在本轮 BoxToMask、训练和预测任务全部终态后再发布停止结果。

**当前状态**
- 已完成：TaskManager 允许 Pending 任务进入 Stopping；ModelTaskController 会将没有后台执行者的 Pending 子任务立即收敛为 Stopped。
- 已完成：FewShotLearningController 取消后保留本轮状态，等待所有子任务终态，再清理运行状态和发布停止结果。
- 已完成：新增 Pending TaskManager 状态测试和 ModelTaskController 的 BoxToMask Pending 停止测试。

**验证证据**
- `cmake --build build --config Release --target dltool_model_tasks_tests --parallel 4` → Release 构建通过。
- `ctest --test-dir build -C Release -R '^(dltool_model_tasks_tests|dltool_feature_lifecycle_tests)$' --output-on-failure` → 2/2 通过。
- `git diff --check -- src/model/include/model/TaskManager.h src/model/TaskManager.cpp src/feature/include/feature/FewShotLearningController.h src/feature/FewShotLearningController.cpp tests/model/test_TaskManager.cpp tests/model/test_ModelTaskController.cpp` → 通过。

**下一步**
- 本阶段已提交为 `b2e878c refactor: 收敛小样本学习任务终态`；继续审查项目关闭期间的 Feature/Data 操作等待和迟到回调。

---

## 2026-09-08 — 隔离智能标注模型加载回调

**目标**
- 防止智能标注清空缓存后重新加载同一模型时，旧加载结果覆盖当前加载。
- 让模型加载 worker 具备可测试的 Adapter seam，并沿现有控制器生命周期收敛。

**当前状态**
- 已完成：每次模型加载使用独立取消令牌；`shutdown()`、`clearCache()` 和替换模型时令牌失效，queued 完成回调同时校验令牌和模型 key。
- 已完成：生产构造继续使用 InferRT 加载器，测试可注入阻塞加载 Adapter；旧加载自然返回但不能发布结果。
- 已完成：新增同 key 重叠加载回归测试，验证清空缓存后只发布当前加载结果。

**验证证据**
- `cmake --build build --config Release --target dltool_feature_lifecycle_tests --parallel 4` → Release 构建通过。
- `ctest --test-dir build -C Release -R '^dltool_feature_lifecycle_tests$' --output-on-failure` → 1/1 通过。
- `git diff --check -- src/feature/include/feature/SmartAnnotationController.h src/feature/SmartAnnotationController.cpp tests/feature/test_FeatureLifecycle.cpp` → 通过。

**下一步**
- 本阶段已提交为 `468071f refactor: 隔离智能标注模型加载回调`；继续审查小样本学习任务终态等待。

---

## 2026-09-08 — 隔离 feature 任务运行轮次

**目标**
- 防止搜索、图像聚类和标注聚类上一轮已经完成但仍排队的进度或结果回调写入下一轮任务。
- 让取消令牌同时承担本轮任务身份校验，保持关闭和重复启动的生命周期语义一致。

**当前状态**
- 已完成：三类 feature 请求启动新轮次前使旧令牌失效；进度回调、queued UI 更新和完成回调均校验所属令牌与关闭状态。
- 已完成：新增公开搜索流程回归测试，模拟两轮任务完成后投递第一轮延迟进度，验证旧回调被丢弃。
- 边界：InferRT 内部计算仍无强制中断接口；令牌负责阻止后续发布和阶段处理，不伪造底层中断能力。

**验证证据**
- 旧实现下新增回归测试未通过；说明延迟的第一轮进度会穿过控制器进入后续事件队列。
- `cmake --build build --config Release --target dltool_feature_lifecycle_tests --parallel 4` → Release 构建通过。
- `ctest --test-dir build -C Release -R '^dltool_feature_lifecycle_tests$' --output-on-failure` → 1/1 通过。
- `git diff --check -- src/feature/include/feature/SearchControllerBase.h src/feature/SearchControllerBase.cpp src/feature/include/feature/ImageClusterController.h src/feature/ImageClusterController.cpp src/feature/include/feature/RoiClusterController.h src/feature/RoiClusterController.cpp tests/feature/test_FeatureLifecycle.cpp` → 通过。

**下一步**
- 本阶段已提交为 `e96b0b4 refactor: 隔离 feature 任务运行轮次`；继续审查小样本学习任务终态等待和智能标注模型加载回调。

---

## 2026-09-08 — 收敛 feature 任务取消信号

**目标**
- 为搜索、图像聚类和标注聚类请求统一设置协作式取消令牌。
- 控制器关闭时先发出取消信号，再等待 worker 线程和后续数据操作收敛。

**当前状态**
- 已完成：搜索、图像聚类和标注聚类在请求启动时创建取消令牌，关闭时置位；worker 在阶段入口和 InferRT 返回后检查取消状态。
- 已完成：生命周期测试执行器验证关闭后观察到取消信号，迟到进度和结果不会继续发布。
- 已完成：删除 ROI 聚类请求重复的 `started_at` 成员。
- 边界：InferRT 当前没有强制取消接口，正在执行的内部计算仍需自然返回后才能完成线程关闭。

**验证证据**
- `cmake --build build --config Release --target dltool_feature_lifecycle_tests --parallel 4` → Release 构建通过。
- `ctest --test-dir build -C Release -R '^dltool_feature_lifecycle_tests$' --output-on-failure` → 1/1 通过。
- `git diff --check -- src/feature/include/feature/RoiClusterController.h src/feature/RoiClusterController.cpp src/feature/include/feature/ImageClusterController.h src/feature/ImageClusterController.cpp src/feature/include/feature/SearchControllerBase.h src/feature/SearchControllerBase.cpp src/feature/RoiSearchController.cpp tests/feature/test_FeatureLifecycle.cpp` → 通过。

**下一步**
- 本阶段已提交为 `5086979 refactor: 收敛 feature 任务取消信号`；继续审查 feature 任务句柄、外部计算取消和小样本学习任务终态等待。

---

## 2026-09-08 — 收敛 feature 关闭期间的进度回调

**目标**
- 阻止搜索、图像聚类和标注聚类的后台进度回调在控制器关闭后继续写入全局进度模型。
- 让 feature 生命周期测试通过 CTest 使用当前构建树 DLL，避免旧 `build/bin` 模块遮蔽新构建结果。

**当前状态**
- 已完成：进度回调统一检查控制器有效性和关闭状态，关闭后丢弃迟到进度消息。
- 已完成：新增可控搜索执行器生命周期测试，覆盖后台回调在关闭期间到达的场景。
- 已完成：修正 feature 测试 PATH 的模块目录顺序，使当前构建树模块优先于共享运行时目录。
- 未完成：InferRT 搜索/聚类接口本身没有取消入口，当前只抑制关闭后的 UI 副作用，底层计算仍由后续阶段按取消能力继续收敛。

**验证证据**
- `cmake -S . -B build -DDLT_BUILD_TESTS=ON` → 配置与生成成功。
- `cmake --build build --config Release --target dltool_feature_lifecycle_tests --parallel 4` → Release 构建成功。
- `ctest --test-dir build -C Release -R '^dltool_feature_lifecycle_tests$' --output-on-failure` → 1/1 通过。
- `git diff --check -- src/feature/README.md src/feature/SearchControllerBase.cpp src/feature/ImageClusterController.cpp src/feature/RoiClusterController.cpp tests/feature/CMakeLists.txt tests/feature/test_FeatureLifecycle.cpp` → 通过。

**下一步**
- 本阶段提交为 `6b724e6 refactor: 收敛 feature 关闭期间进度回调`；继续审查 feature 的任务句柄、外部计算取消和小样本学习任务终态等待。

---

## 2026-09-08 — 收敛 TensorBoard 项目关闭生命周期

**目标**
- 将 TensorBoard 外部进程纳入模型管理器和项目关闭栅栏，避免项目释放后仍遗留进程。

**当前状态**
- 已完成：新增 `TensorBoardRunner` 深生命周期模块，统一启动、切换、终止、强制结束、幂等关闭和关闭后拒绝启动。
- 已完成：`ModelManager` 委托 TensorBoard 进程生命周期，并由 `Project::shutdown()` 在模型任务和评估关闭后、数据管理器关闭前调用。
- 已完成：修正项目级 CTest 的 DLL 搜索路径顺序，确保优先加载当前构建树模块。
- 未完成：阶段 3 其他后台执行者的关闭等待和迟到回调丢弃仍按后续切片继续。

**验证证据**
- `cmake -S . -B build -DDLT_BUILD_TESTS=ON` → 配置与生成成功。
- `cmake --build build --config Release --target dltool_model_storage_params_tests dltool_model_project_shutdown_test dltool_model_tasks_tests --parallel 4` → Release 目标构建成功。
- `ctest --test-dir build -C Release -R '^dltool_model_storage_params_tests$' --output-on-failure` → 1/1 通过，1.83 秒。
- `ctest --test-dir build -C Release -R '^dltool_model_project_shutdown_test$' --output-on-failure` → 1/1 通过，0.33 秒。
- `ctest --test-dir build -C Release -R '^(dltool_model_project_shutdown_test|dltool_model_tasks_tests)$' --output-on-failure` → 模型任务测试通过；首次项目测试因旧 DLL 搜索路径失败，调整 CTest 环境后单独重跑通过。
- `git diff --check` → 待提交文件无新增空白错误。

**下一步**
- 本阶段提交为 `0fb9970 refactor: 收敛 TensorBoard 项目关闭生命周期`；继续按 `final_plan.md` 阶段 3 收敛其他后台执行者的关闭等待和迟到回调丢弃。

---

## 2026-09-08 — 限制外部模型进程关闭等待

**目标**
- 外部模型进程停止不响应时，项目关闭仍能在有界时间内升级到强制结束，避免 `shutdown()` 无限等待。

**当前状态**
- 已完成：`ExternalModelTaskRunner::waitForDone()` 的默认和负数等待使用有限优雅停止窗口，超时复用现有 kill 路径。
- 已完成：新增长时间外部进程关闭回归测试，验证关闭后进程不可继续运行且运行器拒绝新任务。

**验证证据**
- `cmake --build build --config Release --target dltool_model_tasks_tests --parallel 4` → Release 构建成功。
- `ctest --test-dir build -C Release -R '^dltool_model_tasks_tests$' --output-on-failure` → 1/1 通过。

**下一步**
- 继续按 `final_plan.md` 阶段 3 补齐模型任务控制器与外部进程终态回调的组合行为验证。

---

## 2026-09-08 — 拒绝清理活动模型任务记录

**目标**
- 清理任务记录前保留 `Preparing`、`Running`、`Stopping` 任务的可路由身份，避免后台任务仍在运行时丢失控制入口。

**当前状态**
- 已完成：`TaskManager::clearTasks()` 返回清理结果；待处理任务和终态任务可清理，活动任务清理请求被拒绝并保留记录；关闭阶段仍允许统一清理。
- 已完成：新增活动任务身份回归测试，并修正待处理任务可清理的状态边界。

**验证证据**
- `cmake --build build --config Release --target dltool_model_tasks_tests --parallel 4` → Release 构建成功。
- `ctest --test-dir build -C Release -R '^dltool_model_tasks_tests$' --output-on-failure` → 1/1 通过。
- `git diff --check` → 当前工作区已有 `WORKLOG.md` 行尾差异提示；本阶段源码和测试差异未发现新增格式错误。

**下一步**
- 提交本阶段任务清理生命周期变更；继续按 `final_plan.md` 审查其他后台任务的关闭等待与迟到结果丢弃。

---

## 2026-09-08 — 收敛模型任务关闭回调生命周期

**目标**
- 让模型任务关闭等待后台准备 worker 及其 queued completion 全部收敛，避免控制器释放后仍有完成回调访问项目对象。

**当前状态**
- 已完成：`DataOperationWorkflow` 增加按句柄排空完成回调的等待接口；`ModelTaskController::shutdown()` 取消并等待准备操作后再清理句柄。
- 已完成：新增 queued completion 生命周期行为测试。
- 已完成：修正 data/model 测试 CTest PATH 的声明顺序，避免加载 `build/bin` 中的旧模块 DLL。
- 保留：`final_plan.md`、`src/feature/FeatureManager.cpp`、`tools/dependencies.yaml` 等既有工作区改动未纳入本阶段。

**验证证据**
- 旧 CTest 环境下两个目标均以 `0xc0000139` 启动失败；依赖检查确认测试可能加载旧 `build/bin` DLL。
- `cmake -S . -B build -DDLT_BUILD_TESTS=ON` → 配置与生成成功。
- `cmake --build build --config Release --target dltool_data_data_operation_workflow_tests dltool_model_tasks_tests --parallel 4` → Release 构建成功。
- `ctest --test-dir build -C Release -R '^(dltool_data_data_operation_workflow_tests|dltool_model_tasks_tests)$' --output-on-failure` → 2/2 通过。
- `git diff --check` → 通过。

**下一步**
- 提交本阶段变更；之后按 `final_plan.md` 阶段 3 继续审查其他后台任务的关闭等待和迟到结果丢弃。

---

## 2026-09-08 — 收敛最终架构改进方案

**目标**
- 将架构改进内容收敛到根目录 `final_plan.md`，作为后续重构的唯一方案入口。

**当前状态**
- 已完成：统一职责边界、深模块、项目与任务生命周期、持久化、评估性能、几何映射、QML、测试和分阶段验收方案。
- 已确认：`luna_final_plan.md`、`gemini_final_plan.md`、`musespark13_final_plan.md` 不存在于当前工作区、`F:\Projects` 或 Git 历史；未虚构其内容。
- 保留：`src/feature/FeatureManager.cpp`、`tools/dependencies.yaml` 等既有工作区改动未纳入本轮。

**验证证据**
- `git diff --check -- final_plan.md` → 通过。
- 未执行 Release 构建和 CTest，本轮仅修改方案文档。

**下一步**
- 按 `final_plan.md` 阶段 3 继续补充后台任务句柄、项目关闭等待和迟到回调丢弃的行为测试。

---

## 2026-09-08 — 收敛 ROI 聚类关闭生命周期

**目标**
- 项目关闭取消数据操作后，ROI 聚类控制器释放前等待共享 `DataManager` 的后台操作及完成回调收敛，避免迟到回调访问已释放的 feature 对象。

**当前状态**
- 已完成：`RoiClusterController::shutdown()` 在聚类线程结束后等待 `DataManager` 操作；新增真实 SQLite/数据操作生命周期行为测试。
- 已完成：取消仍由项目关闭协调器负责，controller 只等待，不越权取消共享数据操作。
- 保留：`final_plan.md`、`tools/dependencies.yaml`、`FeatureManager.cpp` 等既有工作区改动未纳入本阶段。

**验证证据**
- 旧实现下 `ctest --test-dir build -C Release -R '^dltool_feature_lifecycle_tests$' --output-on-failure` → 失败，关闭返回时数据操作尚未收敛。
- `cmake --build build --config Release --parallel 4` → Release 全量构建通过。
- `ctest --test-dir build -C Release -R '^(dltool_feature_lifecycle_tests|dltool_data_data_operation_workflow_tests|dltool_model_tasks_tests|dltool_model_project_shutdown_test)$' --output-on-failure` → 4/4 通过。
- `git diff --check` → 通过。

**下一步**
- 按 `final_plan.md` 阶段 3 继续审查其他 feature、模型任务和项目关闭路径的句柄等待及迟到回调丢弃，再分别补测试和提交。

---

## 2026-09-07 — 汇总最终架构改进方案

**目标**
- 将架构改进方案统一收敛到根目录 `final_plan.md`，作为后续重构的唯一入口。

**当前状态**
- 已完成：整理目标架构、职责边界、生命周期、持久化、评估性能、几何/QML、构建测试、分阶段路线和验收标准。
- 已确认：工作区和 Git 历史中未找到 `luna_final_plan.md`、`gemini_final_plan.md`、`musespark13_final_plan.md`，未虚构缺失来源内容。
- 保留：其他已有的代码、测试和依赖配置改动未纳入本轮。

**验证证据**
- `rg --files -uu -g '*luna_final_plan.md' -g '*gemini_final_plan.md' -g '*musespark13_final_plan.md' .` → 未找到三份独立方案。
- `git diff --check -- final_plan.md` → 通过。
- 文档检查 → `final_plan.md` 共 703 行；本轮未执行构建和测试。

**下一步**
- 按 `final_plan.md` 的阶段路线继续实施；先完成当前项目关闭生命周期切片的行为测试和 Release/CTest 验证。

---

## 2026-09-08 — 收敛项目关闭生命周期

**目标**
- 让项目关闭先取消并等待项目内后台数据操作，再写入项目更新时间并释放项目资源，避免 SQLite 写锁导致关闭死锁。

**当前状态**
- 已完成：`Project::shutdown()` 在关闭栅栏开始时取消 `DataManager` 数据操作；`ProjectManager::closeProject()` 和析构函数先关闭项目后台执行者，再更新项目修改时间。
- 已完成：新增独立 `project-shutdown` 项目级测试，使用真实 SQLite 写锁验证取消、关闭收敛和更新时间持久化。
- 未完成：阶段 3 其余后台执行者的统一关闭栅栏和迟到回调丢弃仍待继续；本阶段不包含 `final_plan.md`、`tools/dependencies.yaml` 等既有改动。

**验证证据**
- `cmake --build build --config Release --parallel 4` → Release 全量构建通过。
- `ctest --test-dir build -C Release -R '^(dltool_model_project_shutdown_test|dltool_feature_lifecycle_tests|dltool_model_tasks_tests|dltool_data_data_operation_workflow_tests)$' --output-on-failure` → 4/4 通过。
- `python tools\run_project_tests.py --project-layer project-shutdown --skip-build` → 1/1 通过。
- `git diff --check` → 通过。

**下一步**
- 按 `final_plan.md` 阶段 3，先为剩余项目任务句柄、停止/取消等待和项目关闭后的迟到结果丢弃补充行为测试，再收敛实现。

---

## 2026-09-08 — 收敛最终架构改进方案

**目标**
- 将架构改进内容统一收敛到根目录 `final_plan.md`，作为后续重构的唯一方案入口。

**当前状态**
- 已完成：方案覆盖事实源、深模块职责、项目与任务生命周期、数据库/文件一致性、评估性能、几何与 QML、构建测试、分阶段路线和验收门。
- 已确认：工作区未找到 `luna_final_plan.md`、`gemini_final_plan.md`、`musespark13_final_plan.md`；未虚构缺失方案内容，也未修改用户已有的 `tools/dependencies.yaml`。
- 未完成：本轮仅整理方案，尚未执行代码改造。

**验证证据**
- `rg --files -uu -g '*luna_final_plan.md' -g '*gemini_final_plan.md' -g '*musespark13_final_plan.md' .` → 未找到三份独立源方案。
- `git diff --check -- final_plan.md` → 通过。
- 文档结构检查 → `final_plan.md` 包含 13 个顶层章节、697 行；Release 构建和 CTest 未执行。

**下一步**
- 按 `final_plan.md` 阶段 3 继续实施，先补充项目关闭栅栏、任务句柄等待和迟到回调丢弃的行为测试。

---

## 2026-09-08 — 完成任务项目身份隔离

**目标**
- 完成 `project_id + task_id + run_id` 在 C++、EasyTrain、TCP 通信、外部进程和后台准备句柄中的统一路由，阻止跨项目或旧运行的消息、停止命令和进程控制相互影响。

**当前状态**
- 已完成：`TaskIdentity`、项目级 `TaskManager`、TCP 连接绑定、模型启动参数、EasyTrain 协议客户端及各模型入口均使用完整身份。
- 已完成：外部进程、停止请求和后台准备操作按完整身份索引；相同 `run_id` 在不同项目可并行，冲突身份控制被拒绝。
- 已完成：补充 C++ 跨项目进程隔离、项目任务隔离和 Python TCP 协议行为测试；修复 Python TCP 测试夹具未关闭服务端连接导致的 Windows 测试挂起。
- 保留：`final_plan.md` 与 `tools/dependencies.yaml` 不纳入本阶段提交。

**验证证据**
- `cmake --build build --config Release --target dltool_model_tasks_tests --parallel 4` → Release 构建通过。
- `ctest --test-dir build -C Release -R '^dltool_model_tasks_tests$' --output-on-failure` → 1/1 通过。
- `ctest --test-dir build -C Release -R '^dltool_tools_tests$' --output-on-failure` → 1/1 通过。
- `D:\Software\anaconda3\envs\py312\python.exe -m compileall -q 3rdparty\EasyTrain\src\python` → 编译检查通过。
- `python tools\run_project_tests.py --project-layer project-creation --project-root F:\tmp\task-identity-project --project-name 项目身份测试 --skip-build --recreate-project` → CTest 项目创建层 1/1 通过。
- 主仓库和 EasyTrain `git diff --check` → 通过。

**下一步**
- 分别提交 EasyTrain 子模块和主仓库本阶段改动；随后按 `final_plan.md` 阶段 3 继续完善项目关闭栅栏、后台句柄等待和迟到回调丢弃。

---

## 2026-09-07 — 隔离任务项目身份

**目标**
- 在已有 `task_id + run_id` 之外加入 `project_id`，隔离项目重开或多项目并行时的任务消息、停止命令、终态和结果。

**当前状态**
- 已完成：`TaskIdentity`、`TaskManager`、TCP 通信、任务准备和 `Project` 已开始使用完整项目级任务身份；相关 C++ 行为测试已同步调整。
- 未完成：EasyTrain Python 协议及各模型入口尚未完成对齐；部分测试夹具仍需补齐有效项目身份；当前切片尚未完成 Release 构建、CTest 验证和提交。
- 保留：`final_plan.md` 与 `tools/dependencies.yaml` 的既有工作区改动不纳入本切片。

**验证证据**
- `git diff --check` → 通过。
- 当前切片的 Release 构建、CTest 和 Python 编译检查 → 未执行。

**下一步**
- 修正 C++ 测试中的身份构造和连接身份场景，完成 EasyTrain Python 侧身份校验与参数传递，再执行目标 Release 构建和 CTest 验证。

---

## 2026-09-07 — 统一任务运行身份协议

**目标**
- 让一次任务执行使用不可复用的 `run_id`，并使 C++、Python、停止命令和迟到消息按完整身份路由。

**当前状态**
- 已完成：C++ 任务管理、外部进程、TCP 通信、任务准备和测试改为校验 `task_id + run_id`。
- 已完成：EasyTrain 共享协议客户端、异常入口、Ultralytics、Anomalib、Dinomaly2 和 FS-SAM2 入口携带 `--dltool_run_id`。
- 已完成：新增 Python 协议行为测试，覆盖上报身份、旧运行停止命令隔离和缺少运行身份时拒绝创建客户端。
- 已完成：EasyTrain 子模块已提交为 `c0b4ee9 refactor: 统一任务运行身份协议`，主仓库已提交为 `5bb3307 refactor: 统一任务运行身份协议`。
- 未完成：项目关闭栅栏、所有后台执行者统一等待和跨项目资源释放仍属于阶段 3 后续切片。

**验证证据**
- `ctest --test-dir build -C Release -R '^dltool_tools_tests$' --output-on-failure` → 1/1 通过。
- `cmake --build build --config Release --target dltool_model_tasks_tests --parallel 4` → Release 构建通过。
- `ctest --test-dir build -C Release -R '^dltool_model_tasks_tests$' --output-on-failure` → 1/1 通过。
- `python -m compileall -q ...` → Python 协议及入口编译检查通过。
- 主仓库和 EasyTrain 子模块 `git diff --check` → 通过。
- `ctest --test-dir build -C Release -R '^dltool_tools_tests$' --output-on-failure` → 1/1 通过。
- `git diff --cached --check` → 暂存内容无格式错误。

**下一步**
- 已完成：统一任务运行身份切片已提交；随后按 `final_plan.md` 阶段 3 补充项目关闭栅栏、后台句柄等待和迟到回调丢弃测试。

---

## 2026-09-07 — 汇总最终架构改进方案

**目标**
- 综合现有架构方案与仓库事实，形成后续改造的唯一 `final_plan.md` 入口。

**当前状态**
- 已完成：收敛项目作用域、深模块职责、数据库与文件系统一致性、任务生命周期、评估性能、几何转换、QML、构建和测试验收方案。
- 已完成：补充逻辑 `task_id` 与不可复用执行 `run_id`，要求其贯穿 C++/Python 协议、停止命令、持久化和迟到回调校验。
- 未完成：方案尚未进入代码实现阶段。

**验证证据**
- `git diff --check -- final_plan.md` → 通过。
- 文档结构检查 → 697 行、13 个顶层章节，`run_id` 约束已覆盖任务句柄、协议、实施阶段和验收不变量。
- Release 构建和 CTest → 未执行，本轮仅修改方案文档。

**下一步**
- 按 `final_plan.md` 阶段 3，先为任务清空/重启后的旧消息隔离、停止命令身份校验和项目关闭收敛补充行为测试。

---

## 2026-09-07 — 收敛评估缩略图并发生成

**目标**
- 避免同一评估视觉请求在多个 QML delegate 并发加载时重复读取 TIFF 和生成热力图。

**当前状态**
- 已完成：新增 `EvaluationImageRequestCache`，按实际图像字节数缓存结果，并对相同 key 的并发 miss 进行单次生成协调。
- 已完成：`EvaluationThumbnailImageProvider` 通过内部 PImpl 使用请求缓存，公开头不暴露实现细节。
- 已完成：新增并发回归测试，验证 8 个并发请求只执行一次 loader，且所有请求都得到结果。
- 保留：`final_plan.md` 和 `tools/dependencies.yaml` 的既有工作区改动未纳入本阶段。

**验证证据**
- `cmake --build build --config Release --target dltool_model_evaluation_tests --parallel 4` → Release 构建成功。
- `ctest --test-dir build -C Release -R '^dltool_model_evaluation_tests$' --output-on-failure` → 1/1 通过。
- `ctest --test-dir build -C Release -R '^dltool_model_evaluation_tests$' --repeat until-fail:10 --output-on-failure` → 连续 10 次通过。
- `cmake --build build --config Release --target tst_dltool_model_qml --parallel 4` → Release 构建成功。
- `ctest --test-dir build -C Release -R '^tst_dltool_model_qml_(test_anomalyThumbnailHeatmapLifecycle|test_anomalyMatrixFilterFirstHeatmapPaintsPolygons)$' --output-on-failure` → 2/2 通过。
- `git diff --check` → 通过。

**下一步**
- 继续按 `final_plan.md` 阶段 6 审查评估视觉派生的取消、失效和项目关闭生命周期，再进入下一个独立提交。

---

## 2026-09-07 — 收敛测试评估缓存的切换生命周期

**目标**
- 模型切换、测试任务缓存清理和项目关闭前，停止并等待缓存评估 ViewModel 的后台 worker，避免迟到回调访问已删除对象。

**当前状态**
- 已完成：`ModelTestTaskManager::reload()` 清理缓存前统一关闭并等待所有评估 ViewModel。
- 已完成：删除单个测试任务时先关闭对应评估 ViewModel，再释放对象。
- 已完成：新增管理器级回归测试，验证模型切换会取消活动评估，`setModelUuid()` 返回时 worker 已结束。
- 保留：`final_plan.md` 和 `tools/dependencies.yaml` 的既有工作区改动未纳入本阶段。

**验证证据**
- 先行测试在旧实现下无法收敛，CTest 进程因活动评估未收到取消而持续等待；随后终止该具体 CTest 进程。
- `cmake --build build --config Release --target dltool_model_tasks_tests --parallel 4` → Release 构建成功。
- `ctest --test-dir build -C Release -R '^dltool_model_tasks_tests$' --output-on-failure` → 1/1 通过。
- `git diff --check` → 通过。

**下一步**
- 继续按 `final_plan.md` 阶段 3/6 审查任务终态、项目关闭和评估结果身份校验，补充迟到回调与终态不可重开的行为测试。

---

## 2026-09-07 — 阶段提交后的工作区边界

**目标**
- 保持分阶段提交可独立恢复，明确当前未提交文件不属于已完成的 ViewModel 所有权切片。

**当前状态**
- 已完成：`fcb985f refactor: 转移评估图像记录所有权` 已提交。
- 保留：工作树仅有 `final_plan.md` 和 `tools/dependencies.yaml`，继续作为既有改动保留。

**验证证据**
- `git status --short` → 仅显示 `final_plan.md`、`tools/dependencies.yaml`。
- `git log -1 --oneline` → `fcb985f refactor: 转移评估图像记录所有权`。

**下一步**
- 继续阶段 6 的视觉派生缓存与可见实例按需生成审查，单独验证后再提交。

---

## 2026-09-07 — 转移评估图像记录所有权

**目标**
- 避免评估后台结果和 `EvaluationImageModel` 同时持有完整图像 GT/预测记录副本。

**当前状态**
- 已完成：`ModelEvaluationViewModel::loadEvaluation` 消费后台结果中的图像记录并移动到 Qt Model，评估和过滤行为保持不变。
- 保留：`final_plan.md` 和 `tools/dependencies.yaml` 的既有未提交改动未纳入本阶段。

**验证证据**
- `cmake --build build --config Release --target dltool_model_evaluation_tests --parallel 4` → Release 目标构建成功。
- `ctest --test-dir build -C Release -R "^(dltool_model_evaluation_tests|dltool_model_dataset_tests)$" --output-on-failure` → 2/2 通过。

**下一步**
- 继续按 `final_plan.md` 阶段 6 审查视觉派生缓存和可见实例按需生成边界。

---

## 2026-09-07 — 保留架构方案与本机环境改动

**目标**
- 继续按 `final_plan.md` 分阶段推进，同时不将用户已有的方案文档和本机依赖路径混入阶段提交。

**当前状态**
- 已完成：评估结果分数图生命周期切片已提交为 `28a563f`。
- 保留：当前工作树仍有 `final_plan.md` 和 `tools/dependencies.yaml` 未提交改动，后续继续保留。

**验证证据**
- `git status --short` → 仅显示上述两个既有文件变更。
- `git log -1 --oneline` → `28a563f refactor: 释放评估结果中的完整分数图`。

**下一步**
- 继续审查阶段 6 的异常视觉派生懒加载边界，完成独立测试后再创建下一阶段提交。

---

## 2026-09-07 — 收敛评估结果中的完整分数图

**目标**
- 让评估结果不长期持有完整 TIFF 分数图，只保留图像级分数、预测记录、多边形和评估指标。

**当前状态**
- 已完成：在 `EvaluationEngineTest::anomalyClassificationUsesTheSameScoreMapAsRegions` 增加图像级分数保留的回归断言。
- 已完成：评估结果组装前释放 `anomaly_score_map`，保留图像级分数、预测记录、事件多边形和 TIFF 路径；清理相互矛盾的旧非空断言。
- 保留：`tools/dependencies.yaml` 为既有本机环境改动，本轮不处理。

**验证证据**
- `cmake --build build --config Release --target dltool_model_dataset_tests --parallel 4` → Release 目标构建成功。
- `ctest --test-dir build -C Release -R "^(dltool_model_dataset_tests|dltool_model_evaluation_tests)$" --output-on-failure` → 2/2 通过。
- `git diff --check -- WORKLOG.md final_plan.md src/model/IEvaluationEngine.cpp src/model/include/model/EvaluationData.h tests/model/test_EvaluationEngine.cpp` → 通过。

**下一步**
- 继续按 `final_plan.md` 阶段 6，审查评估结果和异常视觉派生的懒加载边界。

---

## 2026-09-07 — 按测试文件列表优化评估数据读取

**目标**
- 消除评估加载阶段读取项目全部图像和标注后再过滤造成的无关物化开销。

**当前状态**
- 已完成：`ProjectDataBase` 增加按图像 ID 分批读取图像和标注的接口，评估加载器以 `test.txt` 的图像 ID 为查询主轴。
- 已完成：保留缺失图像计数、测试选择过滤、标注解析和全局类别目录语义；覆盖超过单批大小的查询。
- 保留：`tools/dependencies.yaml`、`final_plan.md` 及既有文档改动未纳入本阶段代码提交。

**验证证据**
- `cmake --build build --config Release --target dltool_model_dataset_tests --parallel 4` → Release 目标构建成功。
- `ctest --test-dir build -C Release -R '^(dltool_database_database_schema_tests|dltool_model_dataset_tests)$' --output-on-failure` → 2/2 通过。
- `git diff --check -- src/database/DataBase.cpp src/database/include/database/DataBase.h src/model/EvaluationDataset.cpp tests/model/test_EvaluationDataset.cpp` → 通过。

**下一步**
- 继续按 `final_plan.md` 阶段 6，审查评估结果和异常视觉派生的物化边界，补充可见实例按需生成与取消验证。

---

## 2026-09-07 — 收敛综合架构方案入口

**目标**
- 将最终架构改进方案统一维护在根目录 `final_plan.md`。

**当前状态**
- 已完成：补充方案合并后的统一裁决规则，明确以源码、CMake、DDL、YAML、Python 协议和 CTest 为事实源，并保留深模块、单一事实源、生命周期优先和破坏性重构约束。
- 保留：`tools/dependencies.yaml` 为既有用户改动，未修改。

**验证证据**
- `git diff --check -- final_plan.md` → 通过。
- `rg --files -uu -g '*luna_final_plan.md' -g '*gemini_final_plan.md' -g '*musespark13_final_plan.md' .` → 当前工作区未找到三份独立源方案。
- 本轮仅修改文档，未执行构建或测试。

**下一步**
- 按 `final_plan.md` 的阶段路线继续实施；每个代码阶段先补行为测试，再进行 Release 构建和 CTest 验证。

---

## 2026-09-07 — 复用评估完整分数图并限制缓存

**目标**
- 在评估阈值变化或异常区域重新生成时复用同一预测文件的完整分数图，避免最大值读取与区域处理之间重复 TIFF 解码。

**当前状态**
- 已完成：`EvaluationArtifactCache` 增加按文件大小/修改时间校验的完整 `EvaluationScoreMap` 缓存，容量按约 64 MiB 的像素字节成本限制。
- 已完成：评估读取路径在最大值命中后优先复用完整分数图；预测快照、项目/任务作用域或文件身份变化会失效，超大图不进入缓存。
- 已完成：补充完整分数图缓存指针复用、作用域失效和已有评估路径回归测试。
- 保留：`tools/dependencies.yaml` 为既有用户改动，未纳入本阶段提交。

**验证证据**
- `cmake --build build --config Release --target dltool_model_evaluation_tests --parallel 4` → Release 目标构建成功。
- `ctest --test-dir build -C Release -R '^dltool_model_evaluation_tests$' --output-on-failure` → 1/1 通过。
- `git diff --check` → 通过。

**下一步**
- 继续按 `final_plan.md` 阶段 6，评估完整 `EvaluationResult` 与异常多边形的按需视觉派生边界，避免将不可见实例的重计算放在评估主循环中。

---

## 2026-09-07 — 限制评估缩略图缓存内存

**目标**
- 将评估缩略图 provider 的缓存上限从条目数量改为实际 `QImage` 字节成本，避免大图数量少时仍造成过高内存占用。

**当前状态**
- 已完成：`EvaluationThumbnailImageProvider` 使用 64 MiB 字节预算，插入缓存时按 `QImage::sizeInBytes()` 计费。
- 已完成：新增大图淘汰回归测试，验证超出字节预算后会重新读取源图而不是复用旧缓存。
- 保留：`tools/dependencies.yaml` 为既有用户改动，未纳入本阶段提交。

**验证证据**
- `cmake --build build --config Release --target dltool_model_evaluation_tests --parallel 4` → Release 目标构建成功。
- `ctest --test-dir build -C Release -R '^dltool_model_evaluation_tests$' --output-on-failure` → 1/1 通过。
- `git diff --check` → 通过。

**下一步**
- 继续按 `final_plan.md` 阶段 6，审查 `EvaluationResult` 中完整图像/视觉数据的物化边界，补齐按需生成与取消行为测试。

---

## 2026-09-07 — 收敛评估派生缓存作用域

**目标**
- 将阈值搜索、TIFF 最大值和异常区域多边形缓存从进程级静态状态收敛到项目/测试任务作用域，保证预测快照和文件变化正确失效。

**当前状态**
- 已完成：新增 `EvaluationArtifactCache`，由 `ModelEvaluationViewModel` 按项目/测试任务持有，并由评估引擎绑定强类型作用域。
- 已完成：三类缓存统一具备有界容量；预测快照、项目/任务作用域变化或 TIFF 文件大小/修改时间变化时失效。
- 已完成：新增缓存隔离、快照失效和跨任务相同快照阈值搜索回归测试；更新模型开发文档。
- 未完成：缩略图缓存按实际字节限制及视觉派生懒加载仍属于阶段 6 后续切片。
- 保留：`tools/dependencies.yaml` 为既有用户改动，未纳入本阶段提交。

**验证证据**
- `cmake -S . -B build -DDLT_BUILD_TESTS=ON` → 配置与生成成功。
- `cmake --build build --config Release --target dltool_model_dataset_tests dltool_model_evaluation_tests --parallel 4` → Release 目标构建成功。
- `ctest --test-dir build -C Release -R '^(dltool_model_dataset_tests|dltool_model_evaluation_tests)$' --output-on-failure` → 2/2 通过。
- `git diff --check` → 通过。

**下一步**
- 继续按 `final_plan.md` 阶段 6，补齐 `EvaluationThumbnailImageProvider` 的按字节有界缓存和可见实例视觉派生的懒加载行为测试。

---

## 2026-09-07 — 汇总最终架构改进方案

**目标**
- 将现有架构审查结果和仓库事实源收敛为唯一的 `final_plan.md`，作为后续分阶段重构入口。

**当前状态**
- 已完成：`final_plan.md` 覆盖作用域与所有权、线程/任务生命周期、schema、数据与模型生命周期、评估与视觉派生、QML、构建测试、实施路线和验收标准。
- 已确认：当前工作区及 Git 历史未找到 `luna_final_plan.md`、`gemini_final_plan.md`、`musespark13_final_plan.md`；未改动既有的 `tools/dependencies.yaml`。

**验证证据**
- `rg --files -uu -g '*plan*.md'` → 仅发现 `final_plan.md`。
- `git -c safe.directory=F:/Projects/DeepLearningTool log --all --name-only` → 未发现三份指定源方案。
- `git diff --check -- final_plan.md` → 通过；本轮未执行构建或测试（仅文档整理）。

**下一步**
- 按 `final_plan.md` 当前阶段路线，先为评估缓存隔离、预测产物失效和视觉派生懒加载补充行为测试。

---

## 2026-09-07 — 实现模型生命周期与跨介质恢复

**目标**
- 以深模块统一模型创建、复制、重命名、删除和恢复，保证模型目录、模型数据库与 Qt Model 不出现半成功状态。

**当前状态**
- 已完成：新增 `ModelLifecycle`、`IModelRecordStore`、`IModelStorageAdapter` 及每操作一个 JSON journal；支持 staging/quarantine、创建、复制、重命名、删除和待恢复扫描。
- 已完成：`ModelStorageService` 增加模型存储根目录、目录移动/复制/删除和操作路径能力；新增失败回滚与发布失败恢复测试，并纳入 model storage 测试目标。
- 已完成：`ModelManager` 的创建、复制、重命名和删除统一通过 `ModelLifecycle`；恢复待完成操作后再加载模型列表，只有生命周期成功后才刷新 Qt Model 和缓存。
- 已完成：增加 `ModelManager` 生命周期失败时不刷新模型列表的回归测试。
- 未完成：项目级 full 流程未在本阶段执行；下一阶段尚未开始。
- 保留：`tools/dependencies.yaml` 为既有用户改动，未纳入本轮。

**验证证据**
- `cmake --build build --config Release --target dltool_model_storage_params_tests --parallel 4` → Release 构建成功。
- `ctest --test-dir build -C Release -R '^dltool_model_storage_params_tests$' --output-on-failure` → 1/1 通过，覆盖生命周期回滚、恢复及 ModelManager CRUD/失败不刷新。
- `cmake --build build --config Release --target dltool --parallel 4` → Release 应用目标构建成功。
- `ctest --test-dir build -C Release -L model -LE project --output-on-failure` → 12/12 通过。
- `git diff --check` → 通过。

**下一步**
- 修复 `recoverPending()` 的无操作上下文错误返回及 `readJournal` 无用参数，重新构建目标并通过 CTest；随后将 `ModelLifecycle` 接入 `ModelManager`，补齐 CRUD 一致性与重启恢复验证。

---

## 2026-09-07 — 修复异常检测数据集划分丢失标注

**目标**
- 保证异常检测项目在划分训练集、验证集和测试集时保留 Mask/多边形标注。

**当前状态**
- 已完成：`DataManager::splitDataset` 将异常检测与目标检测、语义分割统一视为需要复制几何标注的项目类型，分类项目仍只复制图像级类别。
- 已完成：项目级数据划分测试精确校验 11 个夹具标注在三个子数据集中的总数不变。
- 保留：`tools/dependencies.yaml` 为既有用户改动，未纳入本轮。

**验证证据**
- `cmake --build build --config Release --target dltool_model_data_export_test dltool_model_data_roundtrip_test dltool_model_data_split_test --parallel 4` → 构建成功。
- `python tools\\run_project_tests.py --project-layer project-creation --recreate-project ...` → 通过。
- 在干净项目根目录依次执行 `data-creation`、`data-import`、`data-export`、`data-roundtrip`、`data-split` → 6 层项目级 CTest 全部通过；`data-split` 验证 11 个标注完整复制。
- `rg` 检查 → 无 `[DEBUG-P4]` 或其他临时调试标记。
- `git diff --check` → 通过。

**下一步**
- 继续按 `final_plan.md` 的阶段路线推进下一切片；提交阶段 4 前再做一次完整差异审查。

---

## 2026-09-07 — 完成数据导入回滚与导出产物校验切片

**目标**
- 收敛阶段 4 的批次导入失败/取消回滚，以及导出成功前的最终产物校验。

**当前状态**
- 已完成：导入记录新增图片、标注、类别和被修改的图像级类别，并在失败/取消时回滚；增加批量类别删除和模型重新加载。
- 已完成：COCO、LabelMe、Mask、Folder 导出在成功通知前校验目录、文件数量和关键 JSON 产物。
- 已完成：更新 `final_plan.md` 为统一的架构改进方案。
- 未完成：新增项目级导入回滚测试尚未编译通过，CTest 尚未执行。
- 保留：`tools/dependencies.yaml` 为既有用户改动，未纳入本轮。

**验证证据**
- `cmake --build build --config Release --target dltool_data_data_io_tests dltool_model_data_import_test dltool_model_data_export_test --parallel 4` → 构建失败于 `tests/project/test_DataImport.cpp:298`，`std::vector<QString>` 不支持 `contains`。
- `git diff --check -- final_plan.md` → 通过；代码测试未因编译失败执行。

**下一步**
- 将导入回滚测试中的容器查找改为当前 C++ 标准支持的写法，重新构建目标后通过 CTest 执行 DataIO 导出、数据导入和数据导出项目级测试；随后检查类别序号压缩及既有类别属性恢复。

---

## 2026-09-07 — 统一数据几何转换与 DataIO 取消边界

**目标**
- 在阶段 4 先收敛导入格式共用的几何规则，并修正 DataIO 取消状态跨操作复用的问题。

**当前状态**
- 已完成：新增 `GeometryKernel`，统一矩形、多边形裁剪、尺寸映射和边界计算；LabelMe、Mask、DatasetIO 和 Mask 多边形提取改用同一规则。
- 已完成：保留小连通 Mask 区域，只有少于 3 个点或退化轮廓才丢弃；LabelMe 跨图像边界多边形保留真实边交点。
- 已完成：`DataIO` 拒绝同一实例上的并发后台操作，并在完成取消后复用实例时重置取消状态；导入取消后丢弃已经排队的迟到批次。
- 未完成：DataWorkspace/DataTransfer 的完整批次写库、失败回滚和最终产物校验仍待下一切片。
- 保留：`tools/dependencies.yaml` 为既有用户改动，未纳入阶段提交。

**验证证据**
- `cmake -S . -B build -DDLT_BUILD_TESTS=ON` → 配置与生成成功。
- `cmake --build build --config Release --target dltool_data_data_io_tests dltool_common_geometry_tests dltool_data_label_me_io_tests --parallel 4` → Release 构建成功。
- `ctest --test-dir build -C Release -R '^(dltool_data_data_io_tests|dltool_common_geometry_tests|dltool_data_label_me_io_tests)$' --output-on-failure` → 3/3 通过。
- `git diff --check` → 待提交文件无格式错误。

**下一步**
- 为 DataTransfer 增加批次写入的明确提交/失败结果和导出最终产物校验测试，再替换 DataManager 当前的半成功路径。

---

## 2026-09-07 — 收敛多方案合并裁决说明

**目标**
- 将架构方案的输入范围、冲突裁决规则和最终实施取舍明确写入根目录 `final_plan.md`。

**当前状态**
- 已完成：补充单一事实源、深模块、项目作用域、快照、性能、几何、QML 和验收方案的统一取舍。
- 已完成：记录未在当前工作区、项目父目录和可见 Git 历史中找到三份独立源方案的事实，避免虚构其内容。
- 未完成：本轮未实施方案中的代码改造。
- 保留：`tools/dependencies.yaml` 为既有用户改动，未纳入本轮。

**验证证据**
- `rg --files -uu F:/Projects/DeepLearningTool F:/Projects ...` → 未找到 `luna_final_plan.md`、`gemini_final_plan.md`、`musespark13_final_plan.md`。
- `git diff --check -- final_plan.md` → 未发现格式错误。
- Release 构建和 CTest → 未执行，本轮仅修改方案文档。

**下一步**
- 按 `final_plan.md` 的阶段路线，从数据快照、任务生命周期和几何行为测试开始实施。

---

## 2026-09-07 — 统一项目任务生命周期与关闭栅栏

**目标**
- 为项目内后台任务建立可取消、可等待、可丢弃迟到结果的生命周期，并按固定顺序关闭 Feature、模型任务、评估、数据操作和任务通信。

**当前状态**
- 已完成：外部模型进程、模型准备、评估线程池、数据操作工作流和 Feature worker 均纳入项目关闭路径；关闭后拒绝新任务并丢弃迟到回调。
- 已完成：`TaskManager` 终态收敛、`DataOperationWorkflow` 操作句柄、评估取消等待、Feature 控制器关闭状态和对应行为测试。
- 保留：`final_plan.md`、`tools/dependencies.yaml` 及不属于本阶段的既有工作区改动未纳入阶段提交。

**验证证据**
- `cmake --build build --config Release --target dltool_data_data_operation_workflow_tests dltool_feature_lifecycle_tests dltool_model_tasks_tests --parallel 4` → Release 目标构建成功。
- `ctest --test-dir build -C Release -R '^(dltool_data_data_operation_workflow_tests|dltool_feature_lifecycle_tests|dltool_model_tasks_tests)$' --output-on-failure` → 3/3 通过。
- `ctest --test-dir build -C Release -L ordinary --output-on-failure` → 43/43 通过。
- `git diff --check` → 待提交源码和测试无格式错误。

**下一步**
- 按 `final_plan.md` 阶段 4 收敛 DataWorkspace、DataTransfer 和 GeometryKernel，先补数据操作、路径和坐标映射行为测试。

---

## 2026-09-07 — 合并架构改进最终方案

**目标**
- 综合架构方案审查结论，形成根目录唯一的 `final_plan.md`，作为后续重构的统一决策入口。

**当前状态**
- 已完成：统一单一事实源、深模块、快照跨线程、项目作用域、任务关闭栅栏、数据库 schema、文件与数据库协调、评估性能、几何规则、Feature、QML、构建和测试验收方案。
- 已完成：补充方案合并时的裁决规则，明确按源码、CMake、DDL、YAML、任务协议和 CTest 作为运行时事实源。
- 未完成：本轮仅整理文档，未实施方案中的后续代码阶段。
- 保留：`tools/dependencies.yaml` 及工作区已有代码改动未纳入本轮文档交付。

**验证证据**
- `git -c safe.directory=F:/Projects/DeepLearningTool diff --check -- final_plan.md WORKLOG.md` → 未发现差异格式错误。
- 章节结构检查 → `final_plan.md` 保持 12 个顶层章节，包含目标架构、事实源、实施路线、测试矩阵和完成定义。
- Release 构建和 CTest → 本轮未执行，属于文档整理。

**下一步**
- 按 `final_plan.md` 阶段 3 完成任务句柄、取消、项目关闭等待和迟到回调的行为测试与实现。

---

## 2026-09-07 — 收敛项目任务作用域与终态事件

**目标**
- 消除跨项目共享的 `TaskManager` 状态，保护任务终态不被重复或迟到事件改变。

**当前状态**
- 已完成：`Project` 为每个项目创建并拥有独立的 `TaskManager`；训练页、测试页和任务中心改用当前项目任务实例，`ModelManager` 改为注入当前项目任务状态。
- 已完成：项目关闭同步释放旧项目，避免旧对象图在新项目创建后仍存活；未知任务、重复终态和终态后的迟到消息被丢弃，重新准备任务时清理旧终态记录。
- 已完成：新增项目状态隔离与迟到事件行为测试。
- 未完成：外部进程、数据操作线程、评估线程池及聚合任务尚未全部纳入可等待的项目关闭栅栏；暂停态和统一任务句柄仍待后续阶段收敛。
- 保留：`tools/dependencies.yaml` 为已有用户改动，不属于本阶段。

**验证证据**
- `cmake --build build --config Release --target dltool_model_tasks_tests dltool --parallel 4` → Release 构建成功（存在 spdlog 编译警告）。
- `ctest --test-dir build -C Release -R '^dltool_model_tasks_tests$' --output-on-failure` → 1/1 通过，包含项目任务隔离和终态迟到事件测试。
- `git -c safe.directory=F:/Projects/DeepLearningTool diff --check` → 未发现差异格式错误。

**下一步**
- 为外部进程停止等待、`DataOperationWorkflow`、评估/聚合任务句柄和项目关闭后的回调丢弃补充行为测试，再实现统一关闭栅栏。

---

## 2026-09-07 — 统一数据库 schema 生命周期

**目标**
- 将项目、最近项目、模型和任务数据库的建表入口收敛到统一的版本化 schema 入口，并验证未知版本、结构错误和迁移回滚行为。

**当前状态**
- 已完成：新增 `DatabaseSchema`，通过 Qt resource 读取现有 DDL，统一处理 `PRAGMA user_version`、空库初始化、版本拒绝、事务回滚和表结构校验。
- 已完成：项目、最近项目、模型和任务数据库改用统一 schema 初始化；设置数据库的动态表也复用同一 schema resource，并校验固定字段结构。
- 已完成：删除 `SqlDef`、`SettingsTableTemplate` 运行时建表入口；项目静态读取与项目管理在 schema 初始化失败时正确返回失败，不再把无效项目当作成功打开。
- 已完成：修复 `DataBase.cpp` 中静态项目读取调用 `SchemaKind` 时遗漏 `detail::` 命名空间的问题，并重编所有受基类布局变化影响的测试目标。
- 保留：`tools/dependencies.yaml` 是已有用户改动，不属于本阶段。

**验证证据**
- `cmake -S . -B build -DDLT_BUILD_TESTS=ON` → 配置与生成成功。
- `cmake --build build --config Release --target dltool_database_database_schema_tests dltool_model_project_creation_test dltool_model_dataset_tests dltool_model_tasks_tests --parallel 4` → Release 目标构建成功。
- `ctest --test-dir build -C Release -R '^(dltool_database_database_schema_tests|dltool_model_dataset_tests|dltool_model_tasks_tests)$' --output-on-failure` → 3/3 通过。
- `python tools\\run_project_tests.py --project-layer project-creation --project-root F:\\tmp\\schema-phase-15 --recreate-project --skip-build` → 1/1 通过；项目根目录与 `user_version=1` 均由新夹具验证。
- `git -c safe.directory=F:/Projects/DeepLearningTool diff --check` → 通过。

**下一步**
- 按 `final_plan.md` 进入阶段 3，先为任务句柄、取消、项目关闭等待和迟到回调补充行为测试，再收敛后台任务生命周期。

---

## 2026-09-06 — 快照化后台导出与评估输入

**目标**
- 消除导出 worker 对 `DataManager`/Qt Model 的回读，并让评估图像尺寸 provider 只消费按值复制的缓存快照。

**当前状态**
- 已完成：新增 `DatasetExportSnapshot` 值实现，导出提交前复制数据集、图像、标注和类别数据，worker 不再捕获 `DataManager`。
- 已完成：新增图像尺寸缓存快照接口，评估 provider 改为捕获 `QHash<qint64, QSize>`，缓存未命中仍由评估引擎按路径读取。
- 已完成：新增快照排序与数据独立性测试，更新 model 模块导出边界说明。
- 未完成：`final_plan.md` 后续阶段的任务句柄、关闭栅栏和数据库/schema 重构尚未开始。

**验证证据**
- `cmake --build build --config Release --target dltool_data_dataset_export_source_tests dltool_model_tasks_tests --parallel 4` → 构建成功。
- `ctest --test-dir build -C Release -R "^dltool_data_dataset_export_source_tests$|^dltool_model_tasks_tests$" --output-on-failure` → 2/2 通过。
- `ctest --test-dir build -C Release -R "^dltool_model_data_export_test$" --output-on-failure` → CTest 自动按 fixture 顺序执行项目创建、数据创建、数据导入和数据导出，4/4 通过。
- `cmake --build build --config Release --target dltool --parallel 4` → Release 应用目标构建成功。
- `git -c safe.directory=F:/Projects/DeepLearningTool diff --check` → 通过；未将 `tools/dependencies.yaml` 纳入本阶段改动。

**下一步**
- 按 `final_plan.md` 阶段 2 先补任务句柄、取消和项目关闭等待的行为测试，再收敛各后台执行者的生命周期。

---

## 2026-09-06 — 汇总架构改进最终方案

**目标**
- 将架构审查、领域拆分、线程生命周期、评估性能、QML、构建和测试验收要求收敛到唯一的 final_plan.md。

**当前状态**
- 已完成：重写 final_plan.md，去除重复路线和过时表述，保留当前仓库事实源索引及分阶段实施出口。
- 已完成：覆盖 ProjectContext、快照跨线程、Schema、任务关闭、DataIO/GeometryKernel、ModelLifecycle、EvaluationEngine、Feature、QML、CTest 和安装验收。
- 已确认：本工作区及 Git 历史未找到独立的 luna_final_plan.md、gemini_final_plan.md、musespark13_final_plan.md；未改动已有的 tools/dependencies.yaml。

**验证证据**
- git diff --check -- final_plan.md → 通过，未发现差异格式错误。
- 章节结构检查 → 顶层章节无重复，文档共 12 个顶层章节。
- Release 构建和测试 → 本轮未执行，属于文档整理。

**下一步**
- 按 final_plan.md 阶段 1 先为导出和评估输入快照补充行为测试，再实施线程边界改造。

---

## 2026-09-06 — 收敛构建拓扑与模型测试选择

**目标**
- 为后续架构改进建立可重复的构建依赖、模型测试选择和 Python 工具测试基线。

**当前状态**
- 已完成：调整 `src` 子目录顺序，使当前领域依赖顺序明确；公开项目接口改为前向声明，并在实现和直接使用具体类型的测试中补充必要头文件。
- 已完成：`run_model_tests.py` 默认按 CTest 的 `model`/`qml` 标签选择测试，覆盖评估行为测试；工具测试固定使用构建目录下的 pytest 临时目录。
- 已完成：新增工具测试 CTest 注册及构建拓扑、模型测试选择回归测试。
- 保留：已有的发布环境配置改动和未跟踪的 `final_plan.md` 未纳入本阶段提交。

**验证证据**
- `cmake -S . -B build -DDLT_BUILD_TESTS=ON` → 配置与生成成功。
- `cmake --build build --config Release --parallel 4` → Release 构建成功。
- `ctest --test-dir build -C Release -R '^dltool_tools_tests$' --output-on-failure` → 1/1 通过。
- `python tools\\run_model_tests.py --skip-build` → 33/33 通过。
- `git diff --check` → 未发现差异格式错误。

**下一步**
- 按 `final_plan.md` 进入线程/生命周期与数据库事实源阶段，先补行为测试，再替换实现。

---

## 2026-09-06 — 规划 0.0.2 构建与部署

**目标**
- 将版本更新为 `0.0.2`，完善发布脚本，构建 Release 包并部署到 `F:\dltool`。

**当前状态**
- 已完成：版本入口更新为 `0.0.2`；发布脚本支持 `--build`、严格依赖缺项失败、配置/运行时校验、版本 marker、链接污染检查、重复运行库去重和 Debug DLL 过滤；部署包已生成到 `F:\dltool`。
- 已完成：更新 `docs/DEVELOPMENT.md` 与 `tools/README.md` 的发布入口和参数索引。
- 保留：原有未提交的 `tools/dependencies.yaml` 与未跟踪的 `final_plan.md`。

**验证证据**
- `python -m pytest tests/tools/test_dependency_defaults.py -q --basetemp build\\pytest-tmp` → 8 passed。
- `python tools\\package_app.py --build --install-dir F:\\dltool --parallel 4` → CMake 重新配置显示版本 `0.0.2`，Release 构建成功。
- `python tools\\package_app.py --install-dir F:\\dltool --parallel 4` → 最终发布完成，依赖候选无误报，脚本校验通过。
- `verify_package(F:\\dltool, build, require_qt_runtime=True, expected_version=0.0.2)` → 2180 个文件校验通过。
- `F:\\dltool` 严格扫描 → 172 个 DLL、0 个成对 Debug DLL、0 个 reparse point。
- 最终部署后的 `F:\\dltool\\dltool.exe` 启动 10 秒烟测 → 进程持续运行，测试结束后正常关闭。
- `git diff --check` → 未发现差异格式错误。

**下一步**
- 后续发布沿用 `python tools\\package_app.py --build --install-dir <目标目录>`，发布包完整性校验默认开启。

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
