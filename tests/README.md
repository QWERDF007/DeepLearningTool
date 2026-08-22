# 测试说明

`tests/` 使用 Qt Test、Qt Quick Test 和 CTest。测试按职责分为四个目录：

| 目录 | 主要内容 | CTest 标签 |
|------|----------|------------|
| `ui/` | UI 工具函数测试 | `ordinary;ui` |
| `model/` | Model 模块 C++ 单元测试 | `ordinary;model` |
| `model_qml/` | Model 评估相关 QML 测试 | `ordinary;qml` |
| `project/` | 异常检测项目级集成测试 | `project;...` |

根目录 [`tests/CMakeLists.txt`](CMakeLists.txt) 负责注册四个测试目录。测试运行统一通过 CTest；Python 脚本只是构建项目并调用 CTest，不应直接启动测试 exe。

## 环境

项目级测试默认使用以下配置：

| 配置 | 默认值 |
|------|--------|
| CMake 配置 | `Release` |
| Python 环境 | `D:\Software\anaconda3\envs\py312` |
| 项目目录 | `F:\tmp\pro` |
| 项目名称 | `测试项目` |
| 数据集名称 | `测试数据集` |
| 测试资产 | `tests/assets/model/` |

项目级测试运行时设置 Qt offscreen、software renderer，并将 `DLT_RUNTIME_ROOT`、`DLT_TEST_PROJECT_ROOT`、`DLT_TEST_PROJECT_NAME`、`DLT_TEST_DATASET_NAME` 等环境变量传给测试。

## 普通测试

普通测试不依赖项目级测试产生的 `.dlpro`、数据集、模型或 `F:\tmp\pro`。执行入口为 [`tools/run_model_tests.py`](../tools/run_model_tests.py)，默认包含 8 个 CTest 目标。

### Model C++ 测试

5 个 CTest 目标共覆盖 29 个 C++ 测试源码：

| CTest 目标 | 覆盖源码 | 覆盖内容 |
|------------|----------|----------|
| `dltool_model_evaluation_tests` | `test_ModelEvaluationProtocol.cpp`、`test_EvaluationRegistries.cpp`、`test_EvaluationResult.cpp`、`test_EvaluationGeometryMatching.cpp`、`test_EvaluationModels.cpp`、`test_EvaluationCharts.cpp`、`test_EvaluationAnomalyConfusion.cpp`、`test_AggregateEvaluation.cpp`、`test_ModelEvaluationViewModel.cpp` | 评估协议、注册表、结果对象、几何匹配、评估模型、图表数据、异常混淆矩阵、聚合评估、评估 ViewModel |
| `dltool_model_dataset_tests` | `test_EvaluationDataset.cpp`、`test_ModelDatasetOrganizer.cpp`、`test_EvaluationEngine.cpp`、`test_ModelDatasetSelection.cpp` | 评估数据集、数据集组织、评估引擎、模型数据集选择 |
| `dltool_model_tasks_tests` | `test_ModelTaskTypes.cpp`、`test_TaskCommunicationProtocol.cpp`、`test_TaskCommunicationServer.cpp`、`test_TaskManager.cpp`、`test_ExternalModelTaskRunner.cpp`、`test_ModelTestTaskRepository.cpp`、`test_ModelTaskPreparation.cpp`、`test_ModelTestTaskManager.cpp`、`test_ModelTaskController.cpp` | 任务类型、任务通信协议和服务、任务管理、外部模型任务、测试任务持久化、任务准备、模型测试任务管理和控制 |
| `dltool_model_storage_params_tests` | `test_ModelStorageService.cpp`、`test_ModelStorageMigration.cpp`、`test_ModelParamDefs.cpp`、`test_ModelParamsSchema.cpp`、`test_ModelDataBases.cpp`、`test_ModelManager.cpp` | 模型文件存储、存储迁移、模型参数定义和 Schema、模型数据库、模型管理 |
| `dltool_model_registry_tests` | `test_RegistryIsolation.cpp` | 模型注册表隔离 |

### Model QML 测试

QML 测试被拆成 3 个进程，避免 Qt Quick 对象跨测试函数残留：

| CTest 目标 | QML 测试 | 覆盖内容 |
|------------|----------|----------|
| `tst_dltool_model_qml` | `tst_EvaluationRefactor.qml` | QML 协议键、ViewModel 运行时属性、异常/检测混淆矩阵点击筛选、三种深度学习方法的面板实例化、内容注入、图表数据转换、评估状态遮罩 |
| `tst_dltool_model_qml_registry` | `tst_EvaluationPanelRegistry.qml` | Detection、Anomaly Detection、Segmentation 三种方法的指标、图像指标、图表、混淆矩阵、实例网格和实例详情面板映射 |
| `tst_dltool_model_qml_smoke` | `tst_EvaluationPanelSmoke.qml` | 异常、检测、分割评估面板，图表、混淆矩阵、实例网格、实例详情、参数面板、训练参数、测试任务和测试面板的加载；同时检查图表数据深拷贝和选项转换 |

### UI 测试

目标为 `tst_dltool_ui`，源码为 `ui/test_Utils.cpp`，当前覆盖：

- `withOpacity`：颜色 RGB 和透明度计算
- `stringValue`、`numberValue`、`boolValue`：Variant 类型转换和无效值回退
- `isIntegerValueType`：整数类型判断
- `valueRangeAt`：参数范围索引和默认值
- `paramDecimals`：整数、小数步长和科学计数法的小数精度；当前期望以步长精度为准，相关精度为 `4`

`run_model_tests.py` 默认不包含 UI 目标。需要单独通过 CTest 运行：

```powershell
ctest --test-dir build -C Release -R "^tst_dltool_ui$" --output-on-failure
```

普通 Model/QML 测试：

```powershell
python tools\run_model_tests.py --skip-build
```

## 项目级测试

项目级测试源码位于 [`tests/project/`](project/)，由 [`tools/run_project_tests.py`](../tools/run_project_tests.py) 调用。项目级测试使用 [`PersistentProjectFixture`](project/PersistentProjectFixture.cpp) 打开或创建项目、读取数据集、启动模型任务并等待异步任务完成。

### 测试资产

`tests/assets/model/` 包含：

- 14 张 JPG 图片：`OK` 5 张、`MT_Blowhole` 5 张、`MT_Crack` 4 张
- 9 张 PNG Mask：`MT_Blowhole` 5 张、`MT_Crack` 4 张
- `OK` 图片没有 Mask；其中 `MT_Crack/exp2_num_265639.png` 生成 2 个标注，因此导入后精确期望 14 张图片、10 个标注：`MT_Blowhole=5`、`MT_Crack=5`
- 导入测试还固定校验图片尺寸、Mask 尺寸、Mask 非零像素数和非零区域包围框；夹具变更时需要同步 `tests/project/test_DataImport.cpp` 中的固定基线

### 项目级目标

| 层级 | CTest 目标 | 源码 | 验证内容 |
|------|------------|------|----------|
| `project-creation` | `dltool_model_project_creation_test` | `test_ProjectCreation.cpp` | 创建异常检测项目，验证项目名称、异常检测方法、项目管理器、数据管理器和 `${project-name}.dlpro` 文件 |
| `set-python-env` | `dltool_model_python_environment_test` | `test_PythonEnvironment.cpp` | 设置 Python 环境路径，验证路径存在、设置值生效，并恢复原始设置 |
| `data-creation` | `dltool_model_data_creation_test` | `test_DataCreation.cpp` | 在已有项目中创建或查找指定数据集，验证数据集 ID；新建数据集必须为空，已有数据集的 ID 必须稳定 |
| `data-import` | `dltool_model_data_import_test` | `test_DataImport.cpp` | 要求目标数据集已存在；校验固定图片和 Mask 夹具，再导入 Folder 图片和独立 Mask，验证 14 张图片、10 个标注及 `MT_Blowhole=5`、`MT_Crack=5` |
| `data-export` | `dltool_model_data_export_test` | `test_DataExport.cpp` | 要求数据集精确包含 14 张图片和 10 个标注；分别导出 Mask、LabelMe 和 COCO，并验证导出文件结构和数量 |
| `data-roundtrip` | `dltool_model_data_roundtrip_test` | `test_DataFormatRoundtrip.cpp` | 将导出的 Mask、LabelMe、COCO 分别回导到独立数据集，精确验证源数据集为 14 张图片和 10 个标注，Mask 回导为 9 张图片和 10 个标注，LabelMe/COCO 回导均为 14 张图片和 10 个标注 |
| `model-creation` | `dltool_model_patchcore_model_test` | `test_PatchcoreModel.cpp` | 要求数据集已存在；创建并配置 `anomalib/patchcore` 模型、训练/测试参数和测试任务，验证模型数据库及存储目录 |
| `model-copy` | `dltool_model_patchcore_copy_test` | `test_PatchcoreCopy.cpp` | 要求原始 PatchCore 模型已存在；复制模型配置和数据集选择，验证副本记录、名称、框架、架构和存储目录 |
| `model-rename` | `dltool_model_patchcore_rename_test` | `test_PatchcoreRename.cpp` | 要求 PatchCore 模型已存在；重命名模型并验证数据库记录和存储目录，再恢复原名称 |
| `model-delete` | `dltool_model_patchcore_delete_test` | `test_PatchcoreDelete.cpp` | 要求原始 PatchCore 模型已存在；删除复制模型，验证数据库记录和完整存储目录均被删除，并保留原始模型 |
| `model-train` | `dltool_model_patchcore_train_test` | `test_PatchcoreTrain.cpp` | 要求 PatchCore 模型已存在；启动训练任务、等待成功结束，并验证 `model.ckpt` |
| `model-predict` | `dltool_model_patchcore_predict_test` | `test_PatchcorePredict.cpp` | 要求模型、测试任务和训练权重已存在；启动预测，验证 14 个 TIFF 预测文件和测试任务数据库 |
| `model-evaluation` | `dltool_model_patchcore_evaluation_test` | `test_PatchcoreEvaluation.cpp` | 要求预测任务已存在；执行评估，验证评估结束、结果可用和图片级指标存在 |

### 项目级执行语义

CTest 中声明了项目级 fixture 依赖，但 runner 对不同模式有明确控制：

- `full`：清理项目目录，启用 fixture，执行完整 11 层主流程，包括模型创建、复制、重命名、删除副本、训练、预测和评估
- 其他单层或分组模式：默认复用已有项目，只执行选择的目标，不自动补齐前置 fixture
- `--recreate-project`：在执行任意非 `set-python-env` 层前清理项目目录，然后执行选择的目标
- `set-python-env`：不依赖项目目录，也不会清理项目

因此单层测试用于验证已有产物是否满足前置条件。例如：

- 没有 `.dlpro` 时执行 `data-creation` 必须失败
- 没有目标数据集时执行 `data-import` 必须失败
- 没有数据图片或标注时执行 `data-export` 必须失败
- 没有模型时执行 `model-train` 必须失败
- 没有模型时执行 `model-copy`、`model-rename` 和 `model-delete` 必须失败
- 没有训练权重时执行 `model-predict` 必须失败
- 没有预测结果时执行 `model-evaluation` 必须失败

`data-import` 在数据集已存在但数据量不足时会使用测试资产补充导入，完成后必须精确得到 14 张图片、10 个标注，其中 `MT_Blowhole=5`、`MT_Crack=5`；测试还校验固定图片和 Mask 的尺寸及 Mask 区域元数据。`data-roundtrip` 会为三种格式创建独立的回导数据集，并精确校验回导数量。这两项是测试本身的操作，不是 runner 自动补齐前置测试。

### 完整流程

默认项目根目录为 `F:\tmp\pro`，默认产物为：

```text
F:\tmp\pro\测试项目.dlpro
F:\tmp\pro\data_exports\mask\测试数据集\
F:\tmp\pro\data_exports\labelme\测试数据集\
F:\tmp\pro\data_exports\coco\测试数据集\
F:\tmp\pro\models\patchcore-model\train\weights\model.ckpt
```

执行完整项目流程：

```powershell
python tools\run_project_tests.py --project-layer full
```

已完成构建时：

```powershell
python tools\run_project_tests.py --project-layer full --skip-build
```

预期目标为 11 个：

```text
project-creation
data-creation
data-import
data-export
model-creation
model-copy
model-rename
model-delete
model-train
model-predict
model-evaluation
```

### 分层命令

单层命令默认复用已有项目和产物：

```powershell
python tools\run_project_tests.py --project-layer project-creation
python tools\run_project_tests.py --project-layer data-creation
python tools\run_project_tests.py --project-layer data-import
python tools\run_project_tests.py --project-layer data-export
python tools\run_project_tests.py --project-layer data-roundtrip
python tools\run_project_tests.py --project-layer model-creation
python tools\run_project_tests.py --project-layer model-copy
python tools\run_project_tests.py --project-layer model-rename
python tools\run_project_tests.py --project-layer model-delete
python tools\run_project_tests.py --project-layer model-train
python tools\run_project_tests.py --project-layer model-predict
python tools\run_project_tests.py --project-layer model-evaluation
python tools\run_project_tests.py --project-layer set-python-env --python-env 'D:\Software\anaconda3\envs\py312'
```

也可以执行功能组：

```powershell
python tools\run_project_tests.py --project-layer project-setup
python tools\run_project_tests.py --project-layer data
python tools\run_project_tests.py --project-layer model
```

首次创建或需要重新创建项目时，显式清理项目目录：

```powershell
python tools\run_project_tests.py --project-layer project-creation --recreate-project
```

项目名、数据集名、项目根目录和 Python 环境均可指定：

```powershell
python tools\run_project_tests.py `
    --project-layer project-creation `
    --project-root F:\tmp\pro-new `
    --project-name "测试项目-新建" `
    --dataset-name "测试数据集-新建" `
    --recreate-project
```

后续层必须使用相同的 `--project-root`、`--project-name` 和 `--dataset-name`，否则测试会打开另一个项目或查找另一个数据集。

## CTest 标签和查询

可以使用 CTest 查询目标和标签，而不启动测试：

```powershell
ctest --test-dir build -C Release -N
ctest --test-dir build -C Release -N -L project
ctest --test-dir build -C Release -N -L model
```

项目级测试的标签包括：

- `project-setup`：项目创建、Python 环境
- `project-data`：数据集、导入、导出、回导
- `project-model`：PatchCore 创建、复制、重命名、删除副本、训练、预测、评估
- `project-creation`、`project-data-import`、`project-model-copy`、`project-model-rename`、`project-model-delete`、`project-model-train` 等细粒度层标签

## 当前覆盖边界

当前测试是功能和流程验证，不生成代码行覆盖率报告。已覆盖正常项目创建、数据处理、PatchCore CPU 训练/预测/评估、文件产物和部分缺少前置条件的失败路径。

目前没有系统覆盖：

- 训练失败、预测失败、评估失败和任务取消后的状态恢复
- 损坏或版本不兼容的 `.dlpro`、模型数据库和测试任务数据库
- 无效模型参数、无效数据格式、图片损坏和 Mask 尺寸不匹配
- GPU/CUDA、不同 Python 环境版本和多进程并发任务
- 大规模数据集、长时间训练和性能/内存压力
- 完整 UI 用户操作链路和真实窗口交互

相关的项目级分层说明也见 [`docs/PROJECT_LEVEL_TESTS.md`](../docs/PROJECT_LEVEL_TESTS.md)，工具入口说明见 [`tools/README.md`](../tools/README.md)。
