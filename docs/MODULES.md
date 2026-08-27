# 模块索引

模块目标、依赖和内部入口以各目录的 `CMakeLists.txt` 和 README 为准。本页只提供导航。

## 构建模块

| 目录 | CMake 目标 | QML URI | 模块 README |
| --- | --- | --- | --- |
| [`common`](../src/common/) | `dltool_common` | 无 | [README](../src/common/README.md) |
| [`core`](../src/core/) | `dltool_core` | `dltool.core` | [README](../src/core/README.md) |
| [`database`](../src/database/) | `dltool_database` | 无 | [README](../src/database/README.md) |
| [`ui`](../src/ui/) | `dltool_ui` | `dltool.ui` | [README](../src/ui/README.md) |
| [`parameter`](../src/parameter/) | `dltool_parameter` | 无 | [README](../src/parameter/README.md) |
| [`settings`](../src/settings/) | `dltool_settings` | `dltool.settings` | [README](../src/settings/README.md) |
| [`model`](../src/model/) | `dltool_model` | `dltool.model` | [README](../src/model/README.md) |
| [`feature`](../src/feature/) | `dltool_feature` | `dltool.feature` | [README](../src/feature/README.md) |
| [`data`](../src/data/) | `dltool_data` | `dltool.data` | [README](../src/data/README.md) |
| [`project`](../src/project/) | `dltool_project` | `dltool.project` | [README](../src/project/README.md) |
| [`tool`](../src/tool/) | `dltool` | `dltool.tool` | [README](../src/tool/README.md) |

`common`、`database` 和 `parameter` 不生成 QML 模块；其余模块的 URI 以各自 CMake 配置和头文件中的 Qt QML 声明为准。

## 模块选择

- 修改项目创建、打开或最近项目：从 [`project`](../src/project/) 开始。
- 修改数据集、图片、标注、导入导出或过滤：从 [`data`](../src/data/) 开始，并检查 [`database`](../src/database/) 的持久化接口。
- 修改模型记录、参数、训练、推理或评估：从 [`model`](../src/model/) 开始。
- 修改图像搜索、ROI 搜索、智能标注或小样本学习：从 [`feature`](../src/feature/) 开始。
- 修改公共 QML 控件、日志、进度或图表适配：从 [`ui`](../src/ui/) 开始。
- 修改全局配置 schema 或动态设置对象：从 [`settings`](../src/settings/) 和 [`parameter`](../src/parameter/) 开始。
- 修改应用启动、顶层导航或资源装配：从 [`tool`](../src/tool/) 开始。

## 跨模块规则

1. 公共 C++ 头文件放在模块的 `include/<module>/`，实现留在所属模块。
2. 数据库表定义和 SQLite 读写集中在 `database`，业务模块通过数据库访问类使用它。
3. 公共 QML 控件放在 `ui`；领域页面留在对应业务模块。
4. 项目级对象由 `Project` 聚合，页面通过公开属性和 Qt Model 访问，不跨边界取得内部实现对象。
5. 新增模型或任务类型时，同时检查模型注册表、参数 YAML、数据准备、评估注册表和对应测试。

## 相关入口

- 构建顺序：[`src/CMakeLists.txt`](../src/CMakeLists.txt)
- CMake helper：[`cmake/AddPluginLibrary.cmake`](../cmake/AddPluginLibrary.cmake)
- 应用入口：[`src/tool/main.cpp`](../src/tool/main.cpp)
- 全局配置：[`config/settings/`](../config/settings/)
- 模型配置：[`config/models/`](../config/models/)
