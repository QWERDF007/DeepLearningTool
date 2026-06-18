# ui 模块

## 模块概述

`dltool.ui` 是 DeepLearningTool 项目的 UI 服务层模块，位于项目架构中的基础设施层之上。该模块提供轻量级的 UI 基础设施服务，用于连接 C++ 后端与 QML 前端，但不包含可复用的视觉控件或业务状态管理。

## 目录结构

```
src/ui/
├── CMakeLists.txt           # 构建配置文件
├── README.md                # 模块文档
├── UILogger.cpp             # 日志服务实现
├── ProgressManager.cpp      # 进度管理服务实现
├── Utils.cpp                # 工具函数实现
└── include/ui/
    ├── UILogger.h           # 日志服务头文件
    ├── ProgressManager.h    # 进度管理服务头文件
    ├── Utils.h              # 工具函数头文件
    └── SignalHelper.h      # 信号辅助类头文件
```

## 核心服务

### UILogger - 日志服务
- **角色**：C++ 到 QML 的日志桥接服务
- **特性**：
  - 单例模式，通过 `QT_QML_SINGLETON` 宏实现
  - 内置 spdlog sink，支持线程安全日志写入
  - FIFO 消息队列，上限 100 条消息
  - 追踪 info 和 error 消息计数
  - 生成 HTML 彩色日志文本（错误消息显示为红色）
  - 提供 `message`、`infoCount`、`errorCount` 属性供 QML 绑定

### ProgressManager - 进度管理服务
- **角色**：长时间运行任务的进度追踪
- **特性**：
  - 单例模式
  - 进度值自动校验，限制在 [0, 100] 范围内
  - 完整的任务生命周期管理（start/update/complete/reset）
  - 分级别消息队列（支持 info/error 等级别）
  - 通过 `Qt::QueuedConnection` 保证跨线程安全
  - 提供 `progress`、`isRunning`、`message` 属性供 QML 绑定

### Utils - 工具函数集
- **角色**：QML 工具函数集合
- **功能**：
  - 颜色透明度处理（`withOpacity`）
  - 路径清理（`getCleanPath`，移除 file:/// 前缀）
  - 文件浏览器打开（`openInFileExplorer`）
  - QVariant 类型转换（`stringValue`、`numberValue`、`boolValue`）
  - 参数精度计算（`paramDecimals`，基于值类型和范围推导显示精度）
  - 值范围查找（`valueRangeAt`）
  - 整数类型判断（`isIntegerValueType`）

### SignalHelper - 信号辅助类
- **角色**：跨组件信号中枢
- **功能**：
  - TabBar 导航信号（`changeTabBarIndex`）
  - 图片/标签列表选择信号（支持 Shift 多选、全选、清除）
  - 图片/标签表格选择信号（支持 Shift 多选、全选、清除）
  - 审核到标注跳转信号（`switchToImage`、`selectLabel`）

## 依赖关系

### 构建依赖
- `Qt6::Core` - Qt 核心库
- `Qt6::Gui` - Qt GUI 库
- `Qt6::Quick` - Qt Quick 库
- `quickui` - 第三方 QuickUI 控件库
- `dltool_common` - 项目通用模块

### 项目内位置
```
common → core → database → settings → ui → model → feature → data → project → tool
```

`ui` 模块构建于基础设施层之上，被上层业务模块引用。其他模块通过 `import dltool.ui` 使用本模块提供的服务。

## 使用方式

### QML 侧使用
```qml
import quickui        // 提供可复用控件
import dltool.ui      // 提供服务

// 使用单例服务
UILogger.infoCount
ProgressManager.progress
Utils.withOpacity(color, 0.5)
```

### C++ 侧使用
```cpp
#include "ui/UILogger.h"
#include "ui/ProgressManager.h"

// 直接使用单例
dltool::ui::UILogger::getInstance()->log(level, message);
dltool::ui::ProgressManager::getInstance()->updateProgress(50);
```

### 跨线程更新
跨线程更新必须通过 `QMetaObject::invokeMethod` + `Qt::QueuedConnection` 或已有服务 API：
```cpp
QMetaObject::invokeMethod(
    ProgressManager::getInstance(),
    "updateProgress",
    Qt::QueuedConnection,
    Q_ARG(int, 75)
);
```

### Schema 驱动 QML
应复用 `Utils` 进行标量转换、值范围查找和显示精度推导，避免重复实现：
```qml
// 使用 Utils 推导参数显示精度
Text { text: value.toFixed(Utils.paramDecimals(valueType, valueRange, value, defaultValue)) }
```

## 设计约束

- **不添加可复用视觉控件** - 所有可复用控件、颜色、字体、图标和对话框按钮枚举统一放入 `3rdparty/QuickUI` 模块
- **不引入业务状态** - 项目、数据集、标签、模型等业务状态不归此模块管理
- **跨线程更新必须走 Qt 事件队列** - 通过 `QueuedConnection` 或服务 API 回到 UI 线程，避免直接跨线程操作 UI 对象