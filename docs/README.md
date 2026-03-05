# DeepLearningTool 文档索引

## 文档列表

| 文档 | 说明 |
|------|------|
| [ARCHITECTURE.md](./ARCHITECTURE.md) | 系统架构总览，模块分层与依赖关系 |
| [CODE_STRUCTURE.md](./CODE_STRUCTURE.md) | 详细代码结构，模块说明，数据流 |
| [API_REFERENCE.md](./API_REFERENCE.md) | API参考文档，C++和QML接口说明 |
| [CODING_STYLE.md](./CODING_STYLE.md) | 代码风格指南 |
| [CONTRIBUTING.md](./CONTRIBUTING.md) | 贡献指南 |

## 快速导航

### 了解项目
1. 阅读 [ARCHITECTURE.md](./ARCHITECTURE.md) 了解整体架构
2. 阅读 [CODE_STRUCTURE.md](./CODE_STRUCTURE.md) 了解代码组织

### 开发参考
1. 阅读 [API_REFERENCE.md](./API_REFERENCE.md) 了解接口定义
2. 阅读 [CODING_STYLE.md](./CODING_STYLE.md) 了解代码规范

### 参与贡献
1. 阅读 [CONTRIBUTING.md](./CONTRIBUTING.md) 了解贡献流程

## 模块文档

### Common 模块
- 位置: `src/common/`
- 职责: 日志、崩溃处理、单例模板、工具函数
- 详见: [CODE_STRUCTURE.md#51-common-模块](./CODE_STRUCTURE.md#51-common-模块-dltool_common)

### Settings 模块
- 位置: `src/settings/`
- 职责: 全局配置管理、设置持久化
- 详见: [CODE_STRUCTURE.md#52-settings-模块](./CODE_STRUCTURE.md#52-settings-模块-dltool_settings)

### Data 模块
- 位置: `src/data/`
- 职责: 数据库操作、数据模型、数据持久化
- 详见: [CODE_STRUCTURE.md#53-data-模块](./CODE_STRUCTURE.md#53-data-模块-dltool_data)

### UI 模块
- 位置: `src/ui/`
- 职责: 自定义控件、主题、UI工具
- 详见: [CODE_STRUCTURE.md#54-ui-模块](./CODE_STRUCTURE.md#54-ui-模块-dltool_ui)

### Project 模块
- 位置: `src/project/`
- 职责: 项目管理、业务流程
- 详见: [CODE_STRUCTURE.md#55-project-模块](./CODE_STRUCTURE.md#55-project-模块-dltool_project)

### Tool 模块
- 位置: `src/tool/`
- 职责: 应用入口、主界面
- 详见: [CODE_STRUCTURE.md#56-tool-模块](./CODE_STRUCTURE.md#56-tool-模块-dltool-可执行程序)

## 相关资源

- 根目录 [DESIGN.md](../DESIGN.md) - 产品设计文档
- 根目录 [README.md](../README.md) - 项目简介
