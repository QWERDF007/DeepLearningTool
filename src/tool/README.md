# tool 模块说明

## 模块定位

`tool` 构建最终可执行程序 `dltool`，QML URI 为 `dltool.tool`。它是应用入口和顶层界面编排层，负责启动 Qt 应用、安装基础设施、加载主 QML，并把各功能模块组合成完整桌面应用。

## 架构设计

- `main.cpp` 安装 `CrashHandler`，初始化 spdlog，接入 `UILogger` sink，创建 `QApplication` 和 `QQmlApplicationEngine`。
- QML 加载成功后，`main.cpp` 将 QML 引擎传给 `ProjectManager`，供项目打开后注册图像 provider。
- `Main.qml` 定义主窗口，包含 Header、Content、Footer，并处理退出确认。
- `Content.qml` 使用 `StackLayout` 组织项目、图库、标注、复核、训练、测试页面。
- `header/` 保存顶部导航、菜单和设置弹窗。
- `footer/` 保存日志和进度状态区。
- `qtquickcontrols2.conf` 配置 Qt Quick Controls。
- `CMakeLists.txt` 将 `assets/assets.qrc` 作为大资源链入可执行程序。

## 功能定义

- 应用进程启动和退出。
- 全局日志和崩溃处理初始化。
- 加载主窗口和顶层 QML 页面。
- 在顶部导航中切换项目、数据、标注、复核、训练、测试等工作区。
- 在底部展示日志计数、日志详情、任务进度和进度详情。
- 提供全局设置入口。

## 与其他模块的关系

- 依赖 `common` 启动日志和崩溃处理。
- 依赖 `ui` 展示公共控件、日志和进度。
- 依赖 `project` 管理当前项目和项目页面。
- 依赖 `data` 加载 Gallery、Label、Review 页面。
- 依赖 `model` 加载 Train、Test 页面。
- 通过资源系统使用 `assets/` 中的字体、图标和其他资源。

## 边界定义

- 本模块只做应用装配和顶层导航，不承载业务规则。
- 不直接访问数据库，不直接修改数据集、标注或模型记录。
- 不实现可复用控件，通用控件应放入 `ui`。
- 不把功能页面写在 `tool/qml` 中；具体领域页面放在对应模块的 `qml/` 目录。

## 扩展约定

- 新增一级页面时，应在所属业务模块提供页面组件，再由 `Content.qml` 编排加载。
- 新增全局启动步骤应放在 `main.cpp` 中，并保持失败处理清晰，避免影响 QML 加载。
- 顶层导航只依赖模块公开的 QML 类型和单例，不应访问模块内部 C++ 实现细节。
