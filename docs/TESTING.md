# 测试指南

测试统一由 CTest 注册和执行。Python 工具只负责准备构建、环境变量和 CTest 参数，不直接启动测试可执行文件。测试目标和标签的公共注册入口是 [`cmake/AddTest.cmake`](../cmake/AddTest.cmake)，模块边界和测试分组位于 [`tests/`](../tests/) 各目录的 CMake 文件。

测试 CMake 使用 `GLOB CONFIGURE_DEPENDS` 自动发现测试源文件和测试模块；新增符合现有目录及文件命名约定的测试源文件后，重新配置即可纳入构建。只有改变 CTest 隔离粒度、项目级 fixture 依赖或 QML 测试函数选择时，才需要调整对应的模块 CMake 元数据。

Model C++ 测试按 `test_Evaluation*`、`test_ModelTask*` 等前缀自动归入汇总目标；项目级测试按 `test_Project*`、`test_Data*`、`test_Patchcore*` 文件名推导 CTest 目标、标签和 fixture。新增项目级测试应遵循对应前缀及操作名约定。

## 测试类型

| 类型 | 目录 | 标签 | 关注点 |
| --- | --- | --- | --- |
| UI | [`tests/ui/`](../tests/ui/) | `ordinary;ui` | UI 工具函数和基础 Qt Test |
| Data | [`tests/data/`](../tests/data/) | `ordinary;data` | 数据集划分等数据模块逻辑 |
| Model C++ | [`tests/model/`](../tests/model/) | `ordinary;model` | 评估、任务、参数、存储和注册表 |
| Model QML | [`tests/model_qml/`](../tests/model_qml/) | `ordinary;qml` | 评估面板、缩略图、任务状态和 QML 交互 |
| 项目级 | [`tests/project/`](../tests/project/) | `project;...` | 从 `.dlpro` 到数据、模型任务和评估的真实流程 |

普通测试不依赖项目级测试目录中的项目、数据集、模型或预测产物。项目级测试使用固定测试夹具和可配置的外部项目根目录。

## 构建

```powershell
cmake -S . -B build -DDLT_BUILD_TESTS=ON
cmake --build build --config Release --parallel 4
```

如果构建已经完成，后续命令使用 `--skip-build`。Windows 下不要直接双击或直接从 `build/tests` 启动测试程序；CTest 会根据 CMake 注册的 `ENVIRONMENT_MODIFICATION` 注入 DLL、QML 和测试资源路径。

## 普通测试

运行所有普通测试：

```powershell
ctest --test-dir build -C Release -L ordinary --output-on-failure
```

按标签或目标筛选：

```powershell
ctest --test-dir build -C Release -L model --output-on-failure
ctest --test-dir build -C Release -R '^tst_dltool_ui$' --output-on-failure
ctest --test-dir build -C Release -R '^dltool_data_dataset_splitter_tests$' --output-on-failure
ctest --test-dir build -C Release -R '^dltool_model_evaluation_tests$' --output-on-failure
```

Model 专用脚本会构建并调用 CTest：

```powershell
python tools\run_model_tests.py
python tools\run_model_tests.py --skip-build
```

需要确认当前 CTest 名称或脚本选择范围时，先列出测试：

```powershell
ctest --test-dir build -C Release -N
ctest --test-dir build -C Release -N -L ordinary
```

## 项目级测试

项目级测试由 [`tools/run_project_tests.py`](../tools/run_project_tests.py) 调用 CTest。默认值如下，均可通过命令行或同名 `DLT_TEST_*` 环境变量覆盖：

| 参数 | 默认值 |
| --- | --- |
| `--project-root` | `F:\tmp\pro` |
| `--project-name` | `测试项目` |
| `--dataset-name` | `测试数据集` |
| `--python-env` | `D:\Software\anaconda3\envs\py312` |
| `--configuration` | `Release` |
| `--build-dir` | `build` |

测试夹具位于 [`tests/assets/model/`](../tests/assets/model/)，图片和 Mask 分开保存：

```text
tests/assets/model/
├── images/
│   ├── OK/
│   ├── MT_Blowhole/
│   └── MT_Crack/
└── masks/
    ├── MT_Blowhole/
    └── MT_Crack/
```

导入测试会先校验固定文件清单、图片尺寸、Mask 尺寸、非零像素数和区域包围框，再校验导入结果。当前固定基线是 14 张图片、9 张 Mask、10 个标注，其中 `MT_Blowhole` 为 5 个标注，`MT_Crack` 为 5 个标注。夹具发生变化时，应同步 [`tests/project/test_DataImport.cpp`](../tests/project/test_DataImport.cpp) 中的基线。

## 项目级层级

`run_project_tests.py` 当前支持的选择值由脚本中的 `PROJECT_LAYER_REGEX` 定义：

| 层级 | CTest 目标 |
| --- | --- |
| `project-creation` | `dltool_model_project_creation_test` |
| `set-python-env` | `dltool_model_python_environment_test` |
| `data-creation` | `dltool_model_data_creation_test` |
| `data-import` | `dltool_model_data_import_test` |
| `data-export` | `dltool_model_data_export_test` |
| `data-roundtrip` | `dltool_model_data_roundtrip_test` |
| `data-split` | `dltool_model_data_split_test` |
| `model-creation` | `dltool_model_patchcore_model_test` |
| `model-copy` | `dltool_model_patchcore_copy_test` |
| `model-rename` | `dltool_model_patchcore_rename_test` |
| `model-delete` | `dltool_model_patchcore_delete_test` |
| `model-train` | `dltool_model_patchcore_train_test` |
| `model-predict` | `dltool_model_patchcore_predict_test` |
| `model-evaluation` | `dltool_model_patchcore_evaluation_test` |
| `project-setup`、`data`、`model` | 对应功能组 |
| `full` | 完整主流程选择 |

逐层执行示例：

```powershell
python tools\run_project_tests.py --project-layer project-creation --recreate-project
python tools\run_project_tests.py --project-layer data-creation
python tools\run_project_tests.py --project-layer data-import
python tools\run_project_tests.py --project-layer data-export
python tools\run_project_tests.py --project-layer data-split
python tools\run_project_tests.py --project-layer model-creation
python tools\run_project_tests.py --project-layer model-train
python tools\run_project_tests.py --project-layer model-predict
python tools\run_project_tests.py --project-layer model-evaluation
```

指定项目、数据集和 Python 环境时，后续层级必须使用相同值：

```powershell
python tools\run_project_tests.py `
    --project-layer project-creation `
    --project-root F:\tmp\test-pro `
    --project-name '测试项目-新建' `
    --dataset-name '测试数据集-新建' `
    --python-env 'D:\Software\anaconda3\envs\py312' `
    --recreate-project
```

## 项目级执行语义

- `full` 默认清理 `--project-root`，然后让 CTest fixture 按依赖关系执行完整主流程。
- 其它层级默认复用现有项目和产物，不自动执行被选层级以外的前置测试。
- `--recreate-project` 会在非 `set-python-env` 层执行前清理项目根目录。
- 非 `full` 模式使用 `--fixture-exclude-any .*`，因此缺少 `.dlpro`、数据集、模型、权重或预测文件时，应该由当前层级显式失败，而不是被 fixture 静默补齐。
- `set-python-env` 只修改测试范围内的 Python 环境设置，不依赖项目目录。

因此，想从空目录验证全链路时使用 `full`；想查看单一层级对前置产物的真实要求时，使用默认复用模式并保持项目根目录、项目名和数据集名一致。

## 完整流程

```powershell
python tools\run_project_tests.py --project-layer full
python tools\run_project_tests.py --project-layer full --skip-build
```

`full` 的主流程顺序为：

```text
project-creation
  -> data-creation
  -> data-import
  -> data-export
  -> data-split
  -> model-creation
  -> model-copy / model-rename / model-delete
  -> model-train
  -> model-predict
  -> model-evaluation
```

`set-python-env` 和 `data-roundtrip` 是可单独执行的测试层，不包含在 `full` 的主流程选择中。

## 测试覆盖边界

当前测试重点是模块行为、文件产物、项目级数据流和 PatchCore 训练/预测/评估链路。测试不等同于代码覆盖率报告，也不替代真实桌面环境验证。尚未由当前测试体系系统覆盖的方向包括：硬件/GPU 差异、损坏数据库和图片、极大数据集压力、所有 Python 失败/取消场景以及完整人工 UI 操作链路。

修改测试夹具、CTest 名称、项目级层级或脚本默认值时，应先更新测试注册和实现，再更新本页索引。
