# 测试

所有测试通过 CTest 注册和执行。Python 脚本只负责构建、设置环境和调用 CTest，不直接启动测试可执行文件。

## 目录

| 目录 | 类型 | CTest 标签 |
| --- | --- | --- |
| [`ui/`](ui/) | UI 工具和 Qt Test | `ordinary;ui` |
| [`data/`](data/) | Data 模块 C++ 测试 | `ordinary;data` |
| [`model/`](model/) | Model 模块 C++ 测试 | `ordinary;model` |
| [`model_qml/`](model_qml/) | Model QML 测试 | `ordinary;qml` |
| [`project/`](project/) | 项目级集成测试 | `project;...` |

CTest 注册由 [`../cmake/AddTest.cmake`](../cmake/AddTest.cmake) 统一提供，模块 CMake 只声明依赖、分组规则和 fixture 关系。公共测试辅助代码位于 [`model_support/`](model_support/) 和 [`test_runner.h`](test_runner.h)。

## 构建和运行

```powershell
cmake -S . -B build -DDLT_BUILD_TESTS=ON
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release -L ordinary --output-on-failure
```

已构建时可以跳过构建：

```powershell
python tools\run_model_tests.py --skip-build
```

Windows 下不要直接运行 `build/tests` 中的测试程序。CTest 会根据测试注册配置 DLL、QML import path、离屏渲染和测试资源环境。

## 按目标筛选

```powershell
ctest --test-dir build -C Release -N
ctest --test-dir build -C Release -N -L ordinary
ctest --test-dir build -C Release -R '^tst_dltool_ui$' --output-on-failure
ctest --test-dir build -C Release -R '^dltool_data_split_tests$' --output-on-failure
ctest --test-dir build -C Release -R '^dltool_model_evaluation_tests$' --output-on-failure
```

CTest 名称以当前构建树为准；新增或拆分测试后先重新配置并用 `ctest -N` 检查。

## 项目级测试

项目级测试源码在 [`project/`](project/)，统一通过 [`tools/run_project_tests.py`](../tools/run_project_tests.py) 执行。项目根目录、测试夹具、完整流程、分层执行、前置条件和 Python 环境配置见 [`docs/TESTING.md`](../docs/TESTING.md)。

项目级测试的固定夹具在 [`assets/model/`](assets/model/)，其中 `images/` 与 `masks/` 分开保存。导入测试会校验夹具文件清单、尺寸和 Mask 区域基线；夹具变化时同步对应测试实现。

## 添加测试

1. 在所属测试目录按现有命名约定添加源文件，重新配置后会由 `GLOB CONFIGURE_DEPENDS` 自动发现。
2. 为独立行为设置明确的 CTest 名称和标签。
3. 若测试需要 DLL、QML 或外部资源，在 CMake 的 `ENVIRONMENT_MODIFICATION` 中配置。
4. 项目级测试若依赖前一步产物，显式声明 CTest fixture；单层执行的前置条件仍由测试自身校验。
5. 使用 CTest 验证目标，不绕过测试注册直接运行可执行文件。

详细的项目结构和源码导航见 [仓库文档入口](../docs/README.md)。
