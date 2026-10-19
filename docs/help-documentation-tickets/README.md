# 离线帮助与用户手册票据

## 入口

- [功能规格](../HELP_DOCUMENTATION_SPEC.md)：问题、用户故事、唯一实现决策、测试边界与范围。
- [仓库文档入口](../README.md)：现有架构、数据、开发及测试约定。
- [任务账本](../../WORKLOG.md)：实际执行证据、未验证项与恢复工作的下一步。

## 票据索引

| 编号 | 票据 |
| --- | --- |
| 01 | [最小离线帮助闭环](01-offline-help-tracer.md) |
| 02 | [项目与图库上手流程](02-project-gallery-guide.md) |
| 03 | [标注与检查操作流程](03-annotation-inspection-guide.md) |
| 04 | [训练、评估与导出流程](04-model-workflow-guide.md) |
| 05 | [高级功能与故障诊断](05-advanced-troubleshooting-guide.md) |

## 领取与验收

- 每张票据的 Blocked by 字段是阻塞关系的唯一维护入口；只领取阻塞项已经完成的票据。
- ready-for-agent 表示规格可领取，不表示阻塞已解除、功能已实现或测试已通过。
- 每张票据包含可独立演示的阅读任务、站点集成和相应真实操作验证，不按 C++、QML、构建或正文拆成水平任务。
- 内容票据基于已有基础闭环执行，互不依赖；未交付章节不放占位，不创建指向未来文件的链接，共用导航只有一个写入口。
- 技术契约引用规格中的 D 编号，测试方式引用 T 编号，不在票据中重新定义工具链、路径、错误文案或参数事实。
- 当前源码、配置、构建和验证证据优先于外部参考；实际进度只记录在任务账本。
- 全部验收条件及规格的组合发布包验收通过，才是完整交付；完成部分票据不能宣告整个用户手册完成。

## 源码与测试定位

以下链接用于进入事实源，不复述其实现。

| 目的 | 入口 |
| --- | --- |
| 应用装配职责 | [tool 模块说明](../../src/tool/README.md) |
| 帮助按钮与当前一级导航 | [Header](../../src/tool/qml/header/Header.qml) |
| 应用 QML 注册与安装 | [tool CMake](../../src/tool/CMakeLists.txt) |
| 公共菜单及菜单项 | [QuiMenu](../../3rdparty/QuickUI/src/controls/QuiMenu.qml)、[QuiMenuItem](../../3rdparty/QuickUI/src/controls/QuiMenuItem.qml) |
| 警告通知 | [SignalHelper](../../src/ui/include/ui/SignalHelper.h) |
| 构建选项及运行目录 | [根 CMake](../../CMakeLists.txt)、[构建树配置](../../cmake/ConfigBuildTree.cmake) |
| 现有发布及完整性检查 | [发布脚本](../../tools/package_app.py)、[工具索引](../../tools/README.md) |
| 领域模块入口 | [模块索引](../MODULES.md) |
| 项目与存储约定 | [数据模型](../DATA_MODEL.md) |
| Qt Test 先例 | [UI 测试](../../tests/ui/) |
| QML QuickTest 先例 | [模型 QML 测试](../../tests/model_qml/) |
| 工具测试先例 | [Python 工具测试](../../tests/tools/) |
| 测试注册与执行 | [测试指南](../TESTING.md)、[注册入口](../../cmake/AddTest.cmake) |
