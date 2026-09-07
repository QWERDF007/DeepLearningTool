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
