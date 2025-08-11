## 编码规范（Coding Style）

本规范覆盖 C++、QML 与 CMake，目标是提高可读性、可维护性与一致性。

### C++ 风格
- 头文件包含：跨模块以模块名为前缀，示例：`#include "common/Logger.h"`。禁止相对路径跨模块。
- 命名：
  - 类型/类：大驼峰（如 `ImageTagsTableModel`）。
  - 函数：小驼峰（如 `loadProjectHistory`）。
  - 变量：有语义的名词/短语（避免缩写）；常量全大写下划线（如 `MAX_CACHE_SIZE`）。
- 结构：
  - 单一职责，类文件不超过 ~500 行；复杂逻辑拆分函数；优先早返回，避免深层嵌套。
  - 头/源分离；最小可见原则；接口置于头，实现置于 cpp。
- 错误与异常：
  - 业务错误返回状态/`std::optional`/`tl::expected` 等（视项目采纳）；
  - 异常仅用于不可恢复错误；记录上下文日志。
- 内存与资源：
  - 所有权用 `std::unique_ptr`；共享所有权谨慎使用 `std::shared_ptr`；非空指针语义明确。
  - 事务/资源使用 RAII 封装，确保异常安全。
- 跨平台：
  - 平台相关实现分别放置（如 `Windows*.cpp`、`Linux*.cpp`），统一对外头文件；编译期通过 CMake/宏选择实现。

### QML 风格
- 目录与 URI：模块各自维护 QML 目录与 URI（如 `dltool.ui`、`dltool.data`）。
- 组件命名：以 `Dlt` 为前缀，按用途后缀（`View`/`Dialog`/`Delegate`/`Header` 等）。
- 交互：键鼠事件集中处理；右键菜单、多选快捷键等行为一致化；避免将复杂业务逻辑驻留在 QML。
- 模型绑定：`QAbstractItemModel` 的 role 从 `Qt::UserRole + 1` 起，并在 C++/QML 中导出语义化 role 名。
- 性能：避免在高频信号中做重运算；列表委托中避免创建过多对象；按需懒加载。

### CMake 约定
- 目标命名：`TARGET_NAME=${PROJECT_NAME}_${PLUGIN_NAME}`；公共头目标 `${TARGET_NAME}_header`（`INTERFACE`）。
- Qt 目标：`qt_add_library` + `qt_add_qml_module`；应用使用 `qt_add_executable`。
- 链接顺序：系统/第三方 → 基础模块 → 当前模块 → 头目标。
- 输出与导出：保持与现有模板一致（运行库、插件输出目录、导出宏）。

### 日志与崩溃
- 统一使用 `spdlog`，在 `common` 中封装模块级 Logger 入口；默认 info，调试阶段可设为 debug；热路径禁止大体量日志。
- 崩溃处理：`CrashHandler` 统一入口；Windows 依赖 `Dbghelp`，Linux 依赖 `dl`。

### 数据层约定（sqlpp11/SQLite）
- DDL 头位于 `src/data/include/data/ddl/`，仅 `DataBase.cpp` 直接包含。
- 连接/事务：通过 `DataManager` 统一管理；事务使用 RAII 保证回滚。
- 模型：数据对象与表模型分离；只暴露必要接口给 UI。

### 资源与国际化
- 资源统一挂入 `assets/assets.qrc`；字体、图标等分目录管理。
- QML 插件产物输出到 `${CMAKE_BINARY_DIR}/${PROJECT_NAME}/${PLUGIN_NAME}`，避免重复 plugin 产物。
- 如需国际化，采用 Qt 翻译体系；翻译文件统一放置 `assets/i18n/`。

### 提交与评审
- 提交信息遵循 `type(scope): summary`。
- PR 前自检：编译无警告、关键路径有测试、文档已更新、架构边界未被破坏。


