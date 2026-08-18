# 项目级测试流程

本文说明异常检测项目级集成测试的完整分层执行方式。项目级测试位于
`tests/project/`，通过 `tools/run_project_tests.py` 调用 CTest 执行；普通 model 模块测试与项目级测试相互独立。

## 测试依赖关系

```text
项目创建
├── 数据集创建
    ├── 数据导入
    │   ├── 数据导出
    │   │   └── 格式回导
    │   └── PatchCore 模型创建
    │       └── PatchCore 训练
    │           └── PatchCore 预测
    │               └── PatchCore 评估
└── Python 环境设置测试（独立）
```

只有 `full` 模式会通过 CTest fixture 自动执行完整链路的前置层。其他单层或分组命令只执行所选目标，不会自动补齐前置条件；缺少项目、数据集、导入数据或模型产物时应直接失败。

## 环境要求

默认配置如下：

| 配置 | 默认值 |
|------|--------|
| Python 环境 | `D:\Software\anaconda3\envs\py312` |
| 项目目录 | `F:\tmp\pro` |
| 项目名称 | `测试项目` |
| 数据集名称 | `测试数据集` |
| 测试资产 | `tests/assets/model/` |
| 构建配置 | `Release` |

项目级 runner 默认复用 `--project-root` 中的已有项目。只有 `full` 模式或显式指定 `--recreate-project` 时才会清理目录；清理范围只包含配置的项目测试目录，默认是 `F:\tmp\pro`。

连续执行单层测试时要保持相同的 `--project-root`、`--project-name` 和 `--dataset-name`。例如先执行项目创建，再执行数据集创建：

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer project-creation --project-root F:\tmp\pro
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer data-creation --project-root F:\tmp\pro
```

当前沙箱无法直接启动工作区外的 py312 环境，因此本机执行时可能需要管理员权限；这属于运行环境限制，不是项目级测试目标的硬性要求。

## 首次执行

首次创建项目或需要重新创建项目时，使用 `--recreate-project`：

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --project-layer project-creation --recreate-project
```

如果已经完成编译，后续命令统一添加 `--skip-build`。`full` 模式会自动清理并从空目录开始。

## 逐层执行

以下命令按完整流程排列。每一层都是独立的 CTest 目标，不会自动补齐前置条件；默认复用上一步产生的项目。

### 1. 创建项目

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer project-creation
```

目标：`dltool_model_project_creation_test`

产物：`F:\tmp\pro\测试项目.dlpro`

### 2. 创建数据集

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer data-creation
```

目标：`dltool_model_data_creation_test`

默认数据集名称：`测试数据集`。可通过 `--dataset-name` 指定。

### 3. 设置 Python 环境

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer python
```

目标：`dltool_model_python_environment_test`

### 4. 导入图片和 Mask

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer data-import
```

目标：`dltool_model_data_import_test`

### 5. 导出 Mask、LabelMe 和 COCO

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer data-export
```

目标：`dltool_model_data_export_test`

### 6. 回导三种格式

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer data-roundtrip
```

目标：`dltool_model_data_roundtrip_test`

### 7. 创建 PatchCore 模型

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer model-creation
```

目标：`dltool_model_patchcore_model_test`

### 8. 训练 PatchCore

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer model-train
```

目标：`dltool_model_patchcore_train_test`

产物：`F:\tmp\pro\models\patchcore-model\train\weights\model.ckpt`

### 9. 预测

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer model-predict
```

目标：`dltool_model_patchcore_predict_test`

产物：PatchCore 测试任务下的预测 TIFF 文件。

### 10. 评估

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer model-evaluation
```

目标：`dltool_model_patchcore_evaluation_test`

## 全量执行

一次执行完整的 8 个主流程测试：

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer full
```

预期结果：

```text
100% tests passed, 0 tests failed out of 8
```

`run_project_tests.py` 未指定 `--project-layer` 时也执行这 8 个主流程测试。

## 分组执行

也可以按功能组执行。分组模式同样不会补齐前置条件，应使用已有项目并保持产物完整：

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer project-setup
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer data
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py --skip-build --project-layer model
```

其中 `project-setup` 包含项目创建和 Python 环境测试，`data` 包含数据导入、导出和回导；`model` 表示 PatchCore 模型相关流程。

## 自定义路径

通过 `--python-env` 和 `--project-root` 覆盖默认路径：

```powershell
& 'D:\Software\anaconda3\envs\py312\python.exe' tools\run_project_tests.py `
    --skip-build `
    --project-layer project-creation `
    --python-env 'D:\Software\anaconda3\envs\py312' `
    --project-root 'F:\tmp\pro-new' `
    --project-name '测试项目-新建' `
    --dataset-name '测试数据集-新建'
```

默认复用 `F:\tmp\pro-new` 中的已有项目；如需重新创建，增加 `--recreate-project`。单层模式不会创建缺失的前置项目或数据集。

也可以使用环境变量 `DLT_TEST_PYTHON_ENV`、`DLT_TEST_PROJECT_ROOT`、`DLT_TEST_PROJECT_NAME` 和 `DLT_TEST_ASSET_ROOT` 配置测试路径。
