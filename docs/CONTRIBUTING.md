## 贡献指南（Contributing）

欢迎你为 DeepLearningTool 做出贡献！本指南阐述了工作流、代码规范与提交流程，帮助我们保持高质量与一致性。

### 分支模型
- `main`: 稳定分支，仅用于发布版本。
- `dev`: 日常开发集成分支。
- `feature/<name>`: 新特性开发。
- `fix/<name>`: 缺陷修复。

### 提交信息规范
- 格式：`type(scope): summary`
- 可选类型：`feat`、`fix`、`refactor`、`docs`、`test`、`build`、`chore`。
- 示例：`feat(data): add ImageTagsTable model with roles`

### 开发流程
1. 从最新的 `dev` 创建分支：`git checkout -b feature/<name> origin/dev`。
2. 本地实现与自测：包括基础构建、关键交互与单元/集成测试。
3. 发起 PR 到 `dev`，在描述中说明：变更缘由、范围、风险点与测试结论。
4. 至少 1 名维护者 Code Review 通过后合并。

### PR 检查清单
- 架构边界：未引入跨层反向依赖；跨模块包含仅通过头目标（`*_header`）。
- CMake：新增模块使用 `qt_add_library`/`qt_add_qml_module` 模式；公共头通过 `add_library(<name>_header INTERFACE)` 暴露。
- 代码风格：遵循 `docs/CODING_STYLE.md`；无警告（或已说明理由）。
- QML：组件命名、URI、资源路径符合约定；复杂逻辑下沉 C++。
- 数据层：事务封装到 RAII；禁止 UI/Project 直接操作连接。
- 日志与异常：`spdlog` 统一入口；异常仅用于不可恢复错误。
- 文档：必要变更已更新 `README.md` 或 `docs/`。

### 本地构建与运行
```bash
# Windows (建议使用 Ninja)
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo

# Linux / macOS
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

备注：需正确配置 Qt6 与编译器；项目自带 `cmake/ConfigQT.cmake` 完成必要探测。

### 运行测试
```bash
cmake --build build --target tests
ctest --test-dir build -V
```
注：UI 测试位于 `tests/ui`，用于覆盖关键控件与交互；可扩展更多非 UI 测试。

### 引入第三方依赖
- 统一通过 `3rdparty/` 与 `cmake/` 管理；优先使用现有包管理脚本或 `Find<Lib>.cmake`。
- 引入前请在 PR 说明安全/许可证与兼容性影响；避免在业务模块中直接 `add_subdirectory` 外部仓库。

### 报告缺陷与提建议
- 建议提供：问题复现步骤、环境信息、日志与最小复现示例。
- 对于架构调整类建议，请附带影响面与迁移方案草案。


