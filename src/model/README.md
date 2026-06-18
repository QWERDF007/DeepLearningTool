# model 模块说明

## 模块定位

`model` 构建目标为 `dltool_model`，默认 QML URI 为 `dltool.model`。它管理项目内模型记录、模型结构注册、训练/测试参数定义和训练/测试页面骨架。

## 架构设计

- `IModel` 是模型实例抽象，暴露任务类型、模型结构名称和配置对象。
- `IModelConfig` 是模型配置抽象，聚合训练参数和测试参数。
- `IParams`、`ITrainParams`、`ITestParams` 和 `ParamGroupModel` 将参数组织为 QML 可编辑的分组模型。
- `ModelParamDefs` 提供整数、浮点、滑块、复选框、下拉框等参数定义的构造 helper。
- `ModelManager` 是项目内模型列表模型，负责从 `ProjectDataBase` 加载模型记录，并提供新增、重命名、删除、复制、实例化模型配置等接口。
- `DetectionModels.cpp` 通过 `DLT_REGISTER_MODEL` 注册目标检测下的 YOLOv5 和 YOLOv8，并提供默认训练/测试参数。
- `qml/` 包含 `TrainPage`、`TestPage`、模型列表、模型创建弹窗、参数表单和训练面板等组件。

## 功能定义

- 管理项目内模型记录，包括名称、网络结构、训练结果、测试结果和时间戳。
- 按项目任务类型列出支持的网络结构。
- 为 QML 提供可编辑的训练/测试参数模型。
- 提供模型创建、重命名、删除、复制等操作。
- 为训练页和测试页提供 UI 基础框架。

## 与其他模块的关系

- 由 `project::Project` 创建并挂在 `Project.currentProject.modelManager` 下暴露给 QML。
- 依赖 `database` 的 models 表持久化模型记录。
- 依赖 `core` 的 `DeepLearningMethod` 按任务类型注册和筛选模型。
- 依赖 `ui` 的 QML 控件构建训练/测试页面。
- `tool/Content.qml` 导入本模块并加载训练、测试页面。

## 边界定义

- 本模块管理模型元数据和参数配置，不负责数据集标注、图片过滤或导入导出。
- 当前不执行真实训练、评估或推理流程；训练结果和测试结果字段是记录入口和后续扩展点。
- 图像相似搜索和智能标注使用 InferRT 控制器，当前属于 `data`，不要和项目模型管理混在一起。
- 不创建或关闭项目数据库，数据库生命周期由 `project` 负责。

## 扩展约定

- 新增模型结构时实现 `IModel`/`IModelConfig`/参数类，并通过 `DLT_REGISTER_MODEL` 注册到对应任务类型。
- 新增参数控件类型时需要同步 `control_type` schema、`ui::Utils` 参数辅助函数和 QML 参数表单渲染逻辑。
- 接入真实训练或测试流程时，应保持 `ModelManager` 负责记录管理，具体任务执行可抽象为独立服务，进度反馈接入 `ui::ProgressManager`。
