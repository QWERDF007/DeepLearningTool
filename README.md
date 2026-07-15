# DeepLearningTool

一个基于 Qt 6 和 C++17 开发的深度学习数据标注工具，采用分层模块化架构设计。

## 核心特性

- 项目管理：创建、打开、管理深度学习标注项目
- 数据集管理：支持多数据集组织和管理
- 图像标注：支持多种标注类型（矩形框、多边形等）
- 标签管理：灵活的标签类别和标签系统
- 统一配置：全局配置管理，支持持久化
- 现代UI：基于 Qt Quick/QML 的流畅界面

## 模块架构

```
应用层 (dltool)
    ↓
业务层 (dltool_project)
    ↓
表现层 (dltool_ui) + 数据层 (dltool_data)
    ↓
参数基础层 (dltool_parameter)
    ↓
配置层 (dltool_settings)
    ↓
基础设施层 (dltool_common)
```

## 技术栈

- **语言**: C++17
- **UI框架**: Qt 6 (Qt Quick/QML)
- **构建系统**: CMake 3.18+
- **数据库**: SQLite (sqlpp11)
- **日志**: spdlog
- **JSON**: nlohmann/json

## 快速开始

详见 [docs/README.md](docs/README.md) 获取完整文档。

## 文档

- [架构总览](docs/ARCHITECTURE.md) - 系统架构和模块依赖
- [代码结构](docs/CODE_STRUCTURE.md) - 详细代码组织和模块说明
- [API参考](docs/API_REFERENCE.md) - C++ 和 QML API 文档
- [代码风格](docs/CODING_STYLE.md) - 代码规范指南
- [贡献指南](docs/CONTRIBUTING.md) - 如何参与贡献

## 许可证

[LICENSE](LICENSE)
