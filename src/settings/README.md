# settings 模块说明

## 模块定位

`settings` 构建目标为 `dltool_settings`，QML URI 为 `dltool.settings`。它管理应用级全局配置，并通过 `SettingsDataBase` 持久化到应用目录下的设置数据库。

## 架构设计

- `GlobalSettings` 是 QML 单例，是所有设置对象的统一入口。
- `ProjectSettings` 管理最近项目数量、自动保存间隔和自动保存开关。
- `DataSettings` 管理缩略图、图片加载线程、标注显示、图库单元格缩放、标注缩略图参数。
- `AdvancedSettings` 聚合高级能力配置，目前包含 `ImageSearchSettings` 和 `SmartAnnotationSettings`。
- `UISettings` 管理亮度、对比度、主题和语言等界面偏好。
- `GlobalSettings` 内部持有 `QTimer`，用于设置变化后的延迟自动保存，减少频繁写库。

## 功能定义

- 向 QML 暴露统一设置对象树：`GlobalSettings.project`、`data`、`advanced`、`ui`。
- 提供 `load()`、`save()`、`reset()` 和自动保存开关。
- 持久化图像搜索参数，包括模型、权重路径、特征层、索引存储、FAISS 后端、top-K 等。
- 持久化智能标注参数，包括模型、后端、设备、mask 阈值、多边形简化参数和预览刷新间隔。
- 为图库、标注页、设置弹窗等 UI 提供运行时配置。

## 与其他模块的关系

- 依赖 `database` 的 `SettingsDataBase` 完成持久化。
- 依赖 `common` 的 QML 单例工具。
- `data` 使用这里的显示配置、图片搜索配置和智能标注配置。
- `tool` 的设置弹窗通过 QML 直接绑定这些属性。
- `ui` 不应依赖具体设置项，只提供基础控件和视觉 token。

## 边界定义

- 只管理应用级偏好，不保存 `.dlpro` 项目内的数据和项目元信息。
- 不直接修改数据集、图片、标注、模型记录。
- 不执行图像搜索或智能标注，只保存相关参数。
- 不把 UI 交互流程写入设置类，设置类只提供属性、加载、保存和重置。

## 扩展约定

- 新增设置项时需要同步更新 C++ 属性、默认值、load/save 映射和 `SettingsDataBase` 表字段或配置行。
- 需要 QML 绑定的属性必须提供 `NOTIFY` 信号。
- 需要频繁变化的设置应接入自动保存节流，不应在每次 setter 中直接写库。
