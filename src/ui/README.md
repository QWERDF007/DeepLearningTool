# ui 模块说明

## 模块定位

`ui` 构建目标为 `dltool_ui`，默认 QML URI 为 `dltool.ui`。它提供跨页面复用的视觉系统、QML 控件、日志和进度单例，以及少量 QML 可调用工具。

## 架构设计

- C++ 单例提供基础 UI 服务：`DltColor`、`DltFont`、`UILogger`、`ProgressManager`、`Utils`、`SignalHelper`。
- `IconsFont.h` 暴露图标字体枚举，供 QML 控件统一使用图标。
- `Def.h` 提供属性宏和对话框按钮枚举。
- `controls/` 保存统一样式的 QML 控件，如按钮、输入框、下拉框、菜单、弹窗、滚动页、分割视图、进度条等。
- `UILogger` 接收 spdlog sink 转发的日志，并为底部日志面板提供消息和计数。
- `ProgressManager` 统一管理长任务进度、运行状态和消息队列。

## 功能定义

- 提供全局颜色、字体和图标 token。
- 提供统一 QML 控件，避免业务页面重复实现样式。
- 提供 QML 工具函数，例如路径清理、颜色透明度处理、打开文件管理器。
- 为跨组件选择和导航提供信号中转，例如标注列表/表格选择同步、Review 跳转到 Label。
- 为导入导出、图像搜索等后台任务提供统一进度反馈。

## 与其他模块的关系

- 被 `tool`、`project`、`data`、`model` 的 QML 页面广泛导入。
- `tool/main.cpp` 把 `UILogger` 接入全局 spdlog sink。
- `data` 的导入导出、图像搜索和智能标注通过 `ProgressManager` 反馈状态。
- 本模块依赖 `common` 的单例宏和 Qt 基础模块，不依赖业务模块。

## 边界定义

- 只放通用视觉和交互基础设施。
- 不保存项目、数据集、标注、模型等业务状态。
- 不访问数据库，不执行导入导出或推理任务。
- 不放业务页面。Gallery、Label、Review 属于 `data`，Project 页面属于 `project`，Train/Test 页面属于 `model`，主窗口编排属于 `tool`。

## 扩展约定

- 新增控件应优先放到 `controls/`，并使用 `DltColor`、`DltFont`、`DltFontIcon` 保持视觉一致。
- 业务模块需要共用的交互信号可以放入 `SignalHelper`，但应保持语义清晰，避免成为任意事件总线。
- 后台线程更新日志或进度时应通过 Qt queued connection 或现有工具类回到 UI 线程。
