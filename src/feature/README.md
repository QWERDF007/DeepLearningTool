# feature 模块说明

## 模块定位

`feature` 构建目标为 `dltool_feature`，默认 QML URI 为 `dltool.feature`。它承载需要模型推理、特征计算或外部训练流程的高级能力，目前包含图像相似搜索、标注 ROI 搜索、智能标注和小样本学习。

## 架构设计

- `ImageSearchController` 基于 InferRT + FAISS 执行以图搜图。
- `SmartAnnotationController` 负责智能标注模型加载、缓存和推理请求，返回 QML 可消费的 mask/polygon 结果。
- `FewShotLearningController` 负责小样本学习数据准备、FS-SAM2 训练/推理进程调度、任务中心对接和预测结果导入触发。
- `ImageSearchDataProvider` 是图像搜索对宿主数据模块的最小依赖接口，提供图像 ID、路径、数据集 ID、项目数据库路径以及搜索结果写回能力。
- `FewShotLearningDataProvider` 是小样本学习对宿主数据模块的最小依赖接口，提供图像、标签、类别、数据集、Mask 导入和导入完成回调能力。
- `data::DataManager` 实现 feature 侧 provider，继续向 QML 暴露 `imageSearch`、`smartAnnotation` 和 `fewShotLearning` 属性，保持页面调用方式稳定。

## 与其他模块的关系

- 依赖 `settings` 读取图像搜索、智能标注和小样本学习配置。
- 依赖 `model` 的任务中心接口对接外部训练/推理任务。
- 依赖 `ui` 和 `quickui` 提供 feature QML 组件使用的基础控件。
- 通过 provider 使用 `data` 的图像列表、标签数据、过滤结果写回和 Mask 导入能力，不直接依赖 `data` 模块。
- 通过 `setup_inferrt(feature)` 接入 InferRT、FAISS、CUDA 和 OpenCV 相关能力。

## 边界定义

- 本模块不管理项目数据库 schema，也不直接修改数据模型。
- 图像搜索结果过滤仍属于 `data::GlobalFilter`，由 `DataManager` 作为 provider 写回。
- 小样本学习预测结果导入仍属于 `data`，由 `DataManager` 作为 provider 触发。
- 新增模型推理类功能优先放在本模块，数据集、标注、标签、导入导出和过滤模型仍放在 `data`。
