# database 模块说明

## 模块定位

`database` 构建目标为 `dltool_database`，是不生成 QML 模块的持久化访问层。它统一封装 SQLite/sqlpp11 连接、项目数据库、最近项目数据库、全局设置数据库和表结构定义。

## 架构设计

- `DataBase` 是基础类，负责数据库路径、目录创建、SQLite 连接池和完整性检查。
- `ProjectDataBase` 面向 `.dlpro` 项目文件，封装项目元数据、数据集、图像、标注类别、标签、模型记录等读写操作。
- `RecentProjectsDataBase` 面向应用级 `history.db`，保存最近打开项目路径。
- `SettingsDataBase` 面向应用级 `settings.db`，按设置分类加载和保存配置行。
- `include/database/ddl/` 保存 sqlpp11 表定义和建表 SQL，`SqlDef` 保存当前内置建表语句映射。

## 功能定义

- 创建和打开 SQLite 数据库文件。
- 初始化项目表结构并写入项目基本信息。
- 提供数据集、图片、标签类别、标注实例、图片标签和模型记录的 CRUD 接口。
- 提供最近项目列表读写接口。
- 提供全局设置读写接口，包括特征搜索、智能标注、缩略图、标注显示、图像增强、UI、项目设置。

## 与其他模块的关系

- `settings` 通过 `SettingsDataBase` 持久化全局配置。
- `project` 通过 `ProjectDataBase` 创建/打开项目，通过 `RecentProjectsDataBase` 管理最近项目。
- `data` 通过 `ProjectDataBase` 读写数据集、图片、标签、标注和标签类别。
- `model` 通过 `ProjectDataBase` 读写模型记录。
- 本模块只依赖 Qt Core 和 sqlpp11，不依赖 UI、QML 或业务聚合层。

## 边界定义

- 本模块负责“如何存取数据”，不负责“何时存取”和“如何展示”。
- 不创建 `QAbstractItemModel`，不直接服务 QML。
- 不解析 LabelMe/COCO 等外部数据格式，导入导出逻辑属于 `data`。
- 不进行复杂业务校验，名称冲突、选择状态、过滤状态等由上层模型处理。
- 数据库 schema 变更必须同时考虑建表 SQL、sqlpp11 表定义、读写接口和旧项目兼容。

## 扩展约定

- 新增持久化实体时优先在 `ddl/` 增加表定义和建表 SQL，再在 `ProjectDataBase` 或专用数据库类封装明确接口。
- 应保持批量写入接口，避免上层循环单条写库造成性能问题。
- 错误信息通过 `QString &err_msg` 返回，调用方负责记录日志和反馈 UI。
