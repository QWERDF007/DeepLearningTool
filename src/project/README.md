# project 模块说明

## 模块定位

`project` 构建目标为 `dltool_project`，默认 QML URI 为 `dltool.project`。它是项目生命周期和项目级对象聚合层，负责创建、打开、关闭、删除项目，并把数据管理和模型管理挂到当前项目上。

## 架构设计

- `Project` 表示一个已创建或已打开的 `.dlpro` 项目，持有项目基础信息、`ProjectDataBase`、`DataManager` 和 `ModelManager`。
- `ProjectManager` 是 QML 单例，是项目操作的统一入口，维护 `currentProject`。
- `RectentProjects` 是最近项目列表模型，类名沿用当前代码拼写，内部通过 `RecentProjectsDataBase` 保存和读取历史记录。
- `ProjectManager` 保存 `QQmlApplicationEngine` 引用，用于项目打开后让数据模块注册图像 provider。
- `qml/` 下包含项目首页、项目创建器、项目打开器、历史项目视图、项目信息表单和任务类型选择组件。

## 功能定义

- 创建新项目并初始化 `.dlpro` SQLite 文件。
- 打开已有项目，恢复项目元数据、数据模型和模型列表。
- 关闭当前项目并释放项目级对象。
- 删除项目文件或从最近项目列表移除记录。
- 校验项目路径、后缀、任务类型和新建/打开状态。
- 更新项目基础信息，读取项目统计信息和标注统计信息。
- 维护最近项目选择状态和展示数据。

## 与其他模块的关系

- 依赖 `database` 创建/打开项目库并读写最近项目库。
- 依赖 `data`，每个 `Project` 内创建一个 `DataManager`。
- 依赖 `model`，每个 `Project` 内创建一个 `ModelManager`。
- 依赖 `core` 校验和展示任务类型。
- 依赖 `ui` 构建项目相关 QML 页面。
- 被 `tool` 的主窗口、Header 和 Content 调用，作为用户进入数据和模型工作区的入口。

## 边界定义

- 本模块负责项目生命周期和聚合，不直接实现数据集、图片、标注、模型参数的细节操作。
- 不定义数据库 schema，也不直接解析导入导出格式。
- 不保存应用级全局设置，最近项目除外，因为它属于项目入口体验。
- 项目打开后，业务页面应通过 `currentProject.dataManager` 或 `currentProject.modelManager` 访问下层能力。

## 扩展约定

- 新增项目级对象时由 `Project` 聚合，并在项目打开/关闭时统一管理生命周期。
- 新增项目校验规则应放在 `Project::isValid()` 或 `ProjectManager::isProjectValid()` 附近，保证 QML 和 C++ 使用同一套逻辑。
- 最近项目列表的字段变化需要同步 `RecentProjectsDataBase` 和 `RectentProjects` 模型角色。
