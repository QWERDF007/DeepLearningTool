# common 模块说明

## 模块定位

`common` 构建目标为 `dltool_common`，是整个工程的基础设施模块。它提供日志、崩溃处理、单例工具和通用文件/路径工具，不生成 QML 模块，也不承载任何业务概念。

## 架构设计

- `include/common/` 暴露公共 C++ API，其他模块通过头文件目标依赖使用。
- `Logger` 封装 spdlog 的默认 sink 和 logger 创建，应用入口在此基础上追加 UI 日志 sink。
- `CrashHandler` 是跨平台崩溃处理入口，内部按平台转发到 `WindowsCCrashHandler` 或 `LinuxCCrashHandler`。
- `Singleton<T>`、`SINGLETON`、`QT_QML_SINGLETON` 提供 C++ 单例和 QML 单例的统一实现方式。
- `Utils` 和 `FileReader` 提供 UUID、字符串转换、路径扫描、图片文件过滤等轻量工具。

## 功能定义

- 初始化默认日志输出，并供上层注册各模块 logger。
- 捕获未处理异常、信号或 CRT 异常，生成崩溃诊断信息。
- 提供常用路径和文件扫描能力，尤其是图片文件集合读取。
- 提供跨平台字符串转换和目录提取工具。

## 与其他模块的关系

- 被 `core`、`settings`、`ui`、`data`、`model`、`project`、`tool` 依赖。
- `tool` 在启动阶段调用 `CrashHandler::setup()` 和 `setupLogger()`。
- `ui` 使用 `QT_QML_SINGLETON` 宏实现多个 QML 单例，并通过 `UILogger` 接收 spdlog 消息。
- `data`、`project` 等模块只使用这里的基础工具和日志能力，不应让 `common` 反向感知业务模块。

## 边界定义

- 只放可被多个模块复用的基础设施代码。
- 不访问数据库，不创建 QML 页面，不包含项目、数据集、模型等业务对象。
- 不保存全局应用状态，除非该状态属于日志或崩溃处理基础设施。
- 平台相关实现应封装在本模块内部，对外保持统一接口。

## 扩展约定

- 新增通用工具前先确认是否至少有两个模块需要复用。
- 新增平台能力时优先在 `CrashHandler` 或独立平台实现类后面封装，不把 `#ifdef` 扩散到上层模块。
- 日志格式和 sink 组合应由应用入口决定，本模块只提供默认能力。
