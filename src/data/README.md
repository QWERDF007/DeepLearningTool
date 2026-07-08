# data 模块说明

## 模块定位

`data` 构建目标为 `dltool_data`，默认 QML URI 为 `dltool.data`。它是项目打开后数据工作区的核心模块，负责数据集、图片、标签类别、标注实例、图片标签、过滤、统计和导入导出，并作为高级功能的数据宿主对接 `feature` 模块。

## 架构设计

- `DataManager` 是模块门面，由 `project::Project` 创建并持有。它接收项目任务类型和 `ProjectDataBase`，统一创建并暴露所有数据模型。
- 数据模型包括 `DatasetsListModel`、`ImageInstancesListModel`、`LabelClassesListModel`、`ImageTagsListModel`、`LabelInstancesListModel`、`ImageLabelsListModel`、`ImageLabelsTableModel`、`ImageInfoListModel`。
- 标注数据抽象在 `LabelData_t` 和 `LabelDataHelper_t` 中，当前支持检测框和分割多边形两类数据编辑逻辑。
- 导入导出以 `DataImporter`/`DataExporter` 为扩展点，当前实现 LabelMe 和 COCO，公共文件扫描、尺寸读取、bbox/points 转换、文件复制放在 `DatasetIO`。
- `GlobalFilter` 聚合数据集、图片标签、标注类别、图像级类别和图像搜索过滤模块，对图像和标注模型统一应用过滤。
- `DataManager` 为图像搜索 provider 提供项目图像、数据集、项目路径和结果写回能力；小样本学习由 `model::ModelTaskController` 在项目上下文中直接读取数据并触发 Mask 导入。
- 图像搜索、智能标注和小样本学习入口位于 `feature` 模块，由 `FeatureManager` 通过 `imageSearch`、`smartAnnotation` 和 `fewShotLearning` 属性向 QML 暴露。
- `LabelInstanceImageProvider` 为 QML 提供标注实例缩略图。
- `qml/` 下包含 Gallery、Label、Review 三个数据工作区页面及其子组件。

## 功能定义

- 创建、重命名、删除数据集。
- 导入图片和外部标注数据，支持批量解析和批量写库。
- 导出数据集为 LabelMe 或 COCO 目录结构。
- 创建、编辑、删除、复制标注实例。
- 管理标签类别、颜色、快捷键和排序。
- 管理图片标签，并维护图片、标签、标注之间的关系。
- 提供图片选择、标注选择、当前图片信息、类别统计和过滤后的可见列表。
- 对接图像相似度搜索和标注 ROI 搜索，并将搜索结果纳入全局过滤。
- 对接智能标注推理，为标注页面提供辅助分割结果。
- 对接小样本学习预测结果导入，具体训练/推理流程由 `model` 模块的任务编排层控制。

## 与其他模块的关系

- 由 `project` 创建并挂在 `Project.currentProject.dataManager` 下暴露给 QML。
- 依赖 `database` 的 `ProjectDataBase` 完成项目内数据持久化。
- 依赖 `core` 的任务类型选择标注数据 helper。
- 依赖 `settings` 获取缩略图、显示、图像搜索和智能标注配置。
- 依赖 `ui` 的控件、日志和进度管理能力。
- 依赖 nlohmann/json 解析和生成 LabelMe/COCO JSON。
- 被 `feature` 的图像搜索能力读取图像数据并写回过滤结果；被 `model` 的小样本学习任务编排层读取训练/测试数据并触发预测结果导入。

## 边界定义

- 本模块负责项目内“数据工作区”的业务和模型，不负责项目文件生命周期。创建、打开、关闭项目属于 `project`。
- 不直接管理应用级设置持久化，只读取 `settings` 暴露的配置。
- 不定义底层数据库 schema，表结构和原子读写接口属于 `database`。
- 不放通用 UI 控件，只放数据工作区页面和组件。
- 不负责模型训练配置和模型列表管理，那些属于 `model`。
- QML 页面应通过 `DataManager` 和模型属性操作数据，避免绕过模型直接访问数据库。

## 扩展约定

- 新增数据格式时实现 `DataImporter` 或 `DataExporter` 子类，并在工厂函数中注册。
- 新增过滤维度时实现 `FilterModule`，接入 `GlobalFilter::FilterType`、条件收集和 UI 过滤项模型。
- 新增标注类型时扩展 `LabelData_t`、`LabelDataHelper_t`、数据库区域类型、导入导出转换和 QML 编辑逻辑。
- 长任务应通过批处理和后台线程控制内存占用，并用 `ProgressManager` 反馈进度。
