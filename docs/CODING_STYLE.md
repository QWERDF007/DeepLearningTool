# 编码规范

本规范覆盖 C++、QML、CMake、数据库和资源管理，目标是让代码和模块边界保持一致。

## C++ 风格

- 头文件包含：跨模块包含必须以模块名为前缀，例如 `#include "database/DataBase.h"`、`#include "data/DataManager.h"`。
- 命名：
  - 类型/类：大驼峰，例如 `ImageTagsListModel`。
  - 函数：小驼峰，例如 `loadProjectHistory`。
  - 变量：使用有语义的名词或短语，避免难以理解的缩写。
  - 常量：沿用局部文件已有风格；新增全局常量优先使用 `constexpr`。
- 结构：
  - 公开接口放在 `include/<module>/`，实现放在对应模块的 `.cpp`。
  - 避免 UI/QML 直接访问数据库；通过 `ProjectManager`、`Project`、`DataManager` 和 Qt 模型传递。
  - 平台相关实现分文件放置，例如 `Windows*.cpp`、`Linux*.cpp`。
- 资源与所有权：
  - QObject 父子关系要清晰，QML 单例的生命周期由 `Singleton`/Qt 管理。
  - 非 QObject 资源优先使用 RAII。
- 日志：
  - 使用 `spdlog`，模块日志入口放在本模块 `Logger` 封装中。
  - 热路径避免大量日志；错误日志需要包含失败对象和错误信息。

## QML 风格

- 模块 URI 与目录对应：`dltool.core`、`dltool.settings`、`dltool.ui`、`dltool.model`、`dltool.data`、`dltool.project`、`dltool.tool`。
- 通用控件放在 `src/ui/controls/`，命名使用 `Dlt` 前缀。
- 业务页面放在所属模块下：
  - 项目页：`src/project/qml/`
  - 数据页：`src/data/qml/`
  - 模型训练/测试页：`src/model/qml/`
  - 应用框架：`src/tool/qml/`
- 复杂业务逻辑下沉到 C++ 模型或 manager；QML 负责组合、绑定和轻量交互。
- `QAbstractItemModel` 的 role 从 `Qt::UserRole + 1` 起，并在 `roleNames()` 中导出语义化名称。
- 列表、网格、弹窗和委托中避免重复创建重对象；耗时操作通过 C++ 层和进度/日志单例反馈。

## CMake 约定

- 库模块优先使用 `add_plugin_library(<plugin>)`。
- 目标名由 helper 生成：`${PROJECT_NAME}_${PLUGIN_NAME}`，例如 `dltool_data`。
- 每个模块同时生成 `${TARGET_NAME}_header` 接口头目标，用于暴露 include 目录。
- 不需要 QML 模块的库显式传 `NO_QML_MODULE`，例如 `common` 和 `database`。
- QML URI 默认是 `dltool.<plugin>`；如需覆盖，使用 `QML_URI`。
- 第三方依赖统一在 `3rdparty/` 和 `cmake/` 管理，业务模块不要直接 `add_subdirectory()` 第三方仓库。
- `cmake/ConfigQT.cmake` 当前包含本机 Qt 路径，提交跨环境改动时应避免把个人路径固化为唯一方案。

## 数据库与数据层

- 数据库模块在 `src/database/`，命名空间为 `dltool::database`。
- DDL 和 sqlpp11 表定义放在 `src/database/include/database/ddl/`。
- `ProjectDataBase`、`RecentProjectsDataBase` 和 `SettingsDataBase` 是数据库访问边界。
- `src/data/` 负责 Qt 模型、导入、过滤、统计和标注数据结构，不直接定义 DDL。
- QML 和 UI 层通过 `DataManager` 暴露的模型和 invokable 方法修改数据。
- 批量变更模型时使用 `beginInsertRows`/`endInsertRows`、`beginRemoveRows`/`endRemoveRows` 或 `beginResetModel`/`endResetModel`，并准确发送 `dataChanged`。

## UI 与资源

- 资源统一挂入 `assets/assets.qrc`，字体位于 `assets/Font/`。
- QML 插件产物输出到 `${CMAKE_BINARY_DIR}/dltool/<plugin>`。
- 颜色、字体和图标优先使用 `DltColor`、`DltFont`、`DltFontIcon`。
- 长任务状态统一通过 `ProgressManager`，界面日志统一通过 `UILogger`。

## 测试

- 当前启用的测试位于 `tests/ui`，测试目标为 `tst_dltool_ui`。
- 新增 UI 控件或高风险行为时，应补充 QML/Qt Quick Test 或 C++ 测试。
- 测试运行：

```powershell
cmake --build build --target tst_dltool_ui
ctest --test-dir build -V
```

## 提交与评审

- 提交信息使用 `type(scope): summary`，例如 `docs(api): update data model roles`。
- PR 前自检：构建通过、测试通过、文档同步、未破坏模块依赖方向。
