# core 模块说明

## 模块定位

`core` 构建目标为 `dltool_core`，默认 QML URI 为 `dltool.core`。它保存跨业务模块共享的核心定义，目前主要是深度学习任务类型定义。

## 架构设计

- 模块体量很小，核心类型集中在 `include/core/CoreDef.h`。
- `DeepLearningMethod` 是 QML 单例，向 QML 暴露任务类型枚举和显示名称。
- 枚举包含 `Classification`、`Detection`、`Segmentation`、`Pose`、`OCR`。
- 当前 `supportedMethodTypes()` 把图像分类、目标检测、语义分割、异常检测作为已支持类型；姿态检测和 OCR 作为可展示但未启用的扩展类型。

## 功能定义

- 为项目创建、数据标注、模型注册提供统一的任务类型 ID。
- 向 QML 返回任务类型列表和中文显示名称。
- 提供 `isSupportedMethod()` 供项目合法性校验和功能开关使用。

## 与其他模块的关系

- `project` 使用任务类型创建和校验项目。
- `data` 根据项目任务类型选择标注数据 helper，例如检测框或分割多边形。
- `model` 按任务类型注册和筛选模型结构，例如目标检测下的 YOLOv5/YOLOv8。
- `tool` 和 QML 页面通过该模块展示可选任务类型。

## 边界定义

- 只定义稳定、跨模块共享的核心枚举和轻量映射。
- 不包含业务流程，不读写数据库，不维护项目状态。
- 不实现具体标注、训练、推理或 UI 控件。
- 新增任务类型时，本模块只负责声明和命名；实际功能支持需要在 `data`、`model`、`project`、QML 页面中分别接入。

## 扩展约定

- 新增任务类型要同步更新 `Method`、`MethodsList`、`MethodToName`。
- 只有功能链路可用时才加入 `TypesList`，否则只作为预留类型展示。
- 任务类型 ID 是项目文件和数据库记录会使用的持久化值，调整已有值需要迁移方案。
