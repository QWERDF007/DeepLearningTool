# feature 模块说明

## 模块定位

`feature` 构建目标为 `dltool_feature`，默认 QML URI 为 `dltool.feature`。它承载需要模型推理或特征计算的高级能力，目前包含图像相似搜索和智能标注。

## 架构设计

- `ImageSearchController` 基于 InferRT + FAISS 执行以图搜图。
- `SmartAnnotationController` 负责智能标注模型加载、缓存和推理请求，返回 QML 可消费的 mask/polygon 结果。
- `ImageSearchDataProvider` 是图像搜索对宿主数据模块的最小依赖接口，提供图像 ID、路径、数据集 ID、项目数据库路径以及搜索结果写回能力。
- `data::DataManager` 实现 `ImageSearchDataProvider`，继续向 QML 暴露 `imageSearch` 和 `smartAnnotation` 属性，保持现有页面调用方式稳定。

## 与其他模块的关系

- 依赖 `settings` 读取图像搜索和智能标注配置。
- 依赖 `ui` 的 `ProgressManager` 反馈长任务进度。
- 通过 `ImageSearchDataProvider` 使用 `data` 的图像列表和过滤结果写回能力，不直接依赖 `data` 模块。
- 通过 `setup_inferrt(feature)` 接入 InferRT、FAISS、CUDA 和 OpenCV 相关能力。

## 边界定义

- 本模块不管理项目数据库 schema，也不直接修改数据模型。
- 图像搜索结果过滤仍属于 `data::GlobalFilter`，由 `DataManager` 作为 provider 写回。
- 新增模型推理类功能优先放在本模块，数据集、标注、标签、导入导出和过滤模型仍放在 `data`。
