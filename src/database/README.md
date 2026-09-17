# database 模块说明

## 模块定位

`database` 构建目标为 `dltool_database`，是不生成 QML 模块的持久化访问层。它统一封装 SQLite/sqlpp11 连接、项目数据库、最近项目数据库、全局设置数据库和表结构定义。

## 架构设计

- `DataBase` 是基础类，负责数据库路径、目录创建、SQLite 连接池和完整性检查。
- `ProjectDataBase` 作为项目数据库持久化门面（Facade），面向 `.dlpro` 项目文件，通过共享 `DatabaseContext` 统筹领域仓储层（Repositories）并统一管理事务边界：
  - `ProjectRepository`：项目元信息、图像根目录与创建/修改时间；
  - `DatasetRepository`：数据集 CRUD 与基础信息查询；
  - `ImageRepository`：图像记录、路径维护、统计与批量删除/迁移；
  - `LabelRepository`：标注类别（label_classes）与标注实例（labels）原子/批量读写；
  - `ModelRepository`：模型记录注册与查询；
  - `TagRepository`：标签类别与图像/标注多对多 Tag 关联维护（经 `TagIdCodec` 编解码）。
- `RecentProjectsDataBase` 面向应用级 `history.db`，保存最近打开项目路径。
- `SettingsDataBase` 面向应用级 `settings.db`，按设置分类加载和保存配置行。
- `include/database/ddl/` 保存 sqlpp11 表定义和建表 SQL；`DatabaseSchema` 统一读取这些 SQL resource，执行严格的 schema 初始化与结构校验（历史库以重建适配当前结构，杜绝双份 SQL 与模糊兼容）。

## 功能定义

- 创建和打开 SQLite 数据库文件。
- 初始化项目表结构并写入项目基本信息。
- 提供数据集、图片、标签类别、标注实例、图片标签和模型记录的 CRUD 接口。
- 提供最近项目列表读写接口。
- 提供全局设置读写接口，包括特征搜索、智能标注、缩略图、标注显示、图像增强、UI、软件设置。

## 与其他模块的关系

- `settings` 通过 `SettingsDataBase` 持久化全局配置。
- `project` 通过 `ProjectDataBase` 创建/打开项目，通过 `RecentProjectsDataBase` 管理最近项目。
- `data` 通过 `ProjectDataBase` 读写数据集、图片、标签、标注和标签类别。
- `model` 通过 `ProjectDataBase` 读写模型记录。
- 本模块只依赖 Qt Core 和 sqlpp11，不依赖 UI、QML 或业务聚合层。

## 模型与测试任务数据库

模型目录下的 `model.db` 和每个测试任务目录下的 `task.db` 使用固定的新结构，
不兼容旧 YAML 配置或旧评估文件：

```text
model.db
├─ train_params(name_en, value)
├─ datasets(type, dataset_id, class_ids)
└─ test_tasks(task_id, name, ctime, mtime)

task.db
├─ task_info(task_id, ctime, mtime)
├─ test_params(name_en, value)
├─ datasets(type, dataset_id, class_ids)
└─ prediction(image_id, data)
```

`model.db` 只保存模型级训练参数、训练/验证数据集选择和测试任务索引；任务目录
由 `test/<任务名称>/` 直接组合，不在索引中保存目录或运行状态。`task.db` 保存
测试参数、测试数据集/类别选择和 Python 推理写入的预测记录。评估结果只在 C++
后台计算并保存在进程内存中，不写入数据库或报告文件。

## 边界定义

- 本模块负责“如何存取数据”，不负责“何时存取”和“如何展示”。
- 不创建 `QAbstractItemModel`，不直接服务 QML。
- 不解析 LabelMe/COCO 等外部数据格式，导入导出逻辑属于 `data`。
- 不进行复杂业务校验，名称冲突、选择状态、过滤状态等由上层模型处理。
- 数据库 schema 变更必须更新 DDL 正本、必要的 sqlpp11 表定义、读写接口和 schema 行为测试；不保留重复的运行时建表 SQL。

## 扩展约定

- 新增持久化实体时优先在 `ddl/` 增加表定义和建表 SQL，再在 `ProjectDataBase` 或专用数据库类封装明确接口。
- 应保持批量写入接口，避免上层循环单条写库造成性能问题。
- 错误信息通过 `QString &err_msg` 返回，调用方负责记录日志和反馈 UI。
