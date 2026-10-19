# DeepLearningTool 帮助文档

欢迎使用 DeepLearningTool 深度学习工业视觉工具。

DeepLearningTool 是一套面向工业视觉与智能质检场景的桌面端数据集管理、交互标注、模型训练、效果评估与产物交付工具。本手册提供完整的离线使用指引，帮助您高效完成从样本准备到模型落地的全流程工作。

## 阅读指引

根据您的工作角色与使用需求，可选择以下阅读路径：

- **快速上手**：新用户请阅读 [快速上手](quickstart.md)，快速了解界面三段式布局、快捷操作与端到端核心工作流。
- **数据准备**：查阅 [项目管理](guide_project.md)、[图库与数据集管理](guide_gallery.md)、[数据标注](guide_annotation.md) 与 [标注检查](guide_inspection.md)，规范准备高质量训练数据。
- **模型开发**：查阅 [模型训练](guide_training.md)、[模型评估](guide_evaluation.md) 与 [数据与模型导出](guide_export.md)，掌握超参数调节、混淆矩阵分析与权重提取。
- **高级支持**：查阅 [高级功能](guide_advanced.md) 与 [故障诊断](guide_troubleshooting.md)，了解以图搜图、区域检索及典型异常排查方法。

```{toctree}
:maxdepth: 2
:caption: 入门与数据管理

quickstart
guide_project
guide_gallery
```

```{toctree}
:maxdepth: 2
:caption: 数据标注与检查

guide_annotation
guide_inspection
```

```{toctree}
:maxdepth: 2
:caption: 模型工作流

guide_training
guide_evaluation
guide_export
```

```{toctree}
:maxdepth: 2
:caption: 高级特性与维护

guide_advanced
guide_troubleshooting
```

## 离线使用说明

本手册采用完全静态化构建并随应用程序一同分发：

- **无网络依赖**：全部页面、样式、字体、图标和脚本均存储于本地应用目录中，无需连接互联网。
- **全文检索**：支持中文关键词全局搜索（包含中文分词检索）与结果跳转高亮，通过顶部搜索栏可快速定位。
- **多主题适配**：页面支持浅色 (Light)、深色 (Dark) 以及跟随系统外观主题，可在页面右上角自由切换。
