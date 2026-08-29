# 数据模型与存储

本页说明文件边界和持久化关系。具体字段类型和 SQL 以 [`src/database/include/database/ddl/`](../src/database/include/database/ddl/) 及数据库访问类为准。

## 存储边界

```text
应用目录/
└── db/
    ├── settings.db       全局设置
    └── history.db        最近项目路径

项目根目录/
├── <项目名>.dlpro        项目 SQLite 数据库
└── models/
    └── <模型名>/          模型文件和任务产物
```

应用级数据库路径由 `DataBase::applicationDatabasePath()` 计算为应用程序目录下的 `db/`；项目数据库路径由项目管理器传入。不要根据目录名猜测 schema，使用对应访问类和 DDL。

## 项目文件

`.dlpro` 是 SQLite 文件，不是压缩包或自定义文本格式。一个项目数据库保存：

- `project`：项目名称、任务类型、描述、项目路径、图像基础路径和时间信息。
- `datasets`：数据集名称和扩展数据。
- `images`：数据集中的图像 ID、路径和扩展数据。
- `label_classes`：标注类别、颜色、快捷键和排序。
- `labels`：图像关联的类别、区域类型、区域数据和排序。
- `tag_classes`、`tags`：图像标签和标注标签。
- `models`：模型 UUID、名称、框架、架构和扩展数据。

`ProjectDataBase` 是项目数据库的访问边界；`DataManager`、`ModelManager` 和项目 QML 不直接管理 SQLite 连接。数据集删除时，项目数据库访问层负责删除其关联的图片、标注和 Tag 关系。

## 模型目录

模型目录由 `ModelStorageService` 统一计算，模型名和测试任务目录名经过路径校验：

```text
models/<模型名>/
├── model.db
├── datasets/
│   └── masks/
├── train/
│   ├── train.txt
│   ├── validation.txt
│   ├── weights/
│   └── logs/
└── test/
    ├── logs/
    └── <测试任务目录>/
        ├── task.db
        ├── test.txt
        └── pred/
```

训练和测试文件由任务准备阶段生成。 `train.txt`、`validation.txt` 和 `test.txt` 保存数据导出后的图像列表；训练/验证使用模型共享的 `datasets/masks/`。测试任务使用自己的目录，避免不同测试任务共享或覆盖 `test.txt`、`task.db` 和 `pred/`。

## 模型数据库与任务数据库

模型目录中的 `model.db` 只保存模型侧索引和配置：

```text
train_params   训练参数，按 group/name_en 保存
datasets       训练、验证等数据集及类别选择
test_tasks     测试任务 ID、名称和时间
```

测试任务目录中的 `task.db` 保存任务自身状态输入和预测记录：

```text
task_info      任务 ID 和时间
test_params    推理/评估参数，按 group/name_en 保存
datasets       测试数据集及类别选择
prediction     image_id 到预测数据的映射
```

预测产物文件存放在同一任务目录的 `pred/`。异常检测的 C++ 评估、分割和热力图以 `pred/<image_id>.tiff` 为唯一原始分数来源；`prediction` 表仅保存任务侧记录，数据集选择仍由 `task.db` 提供。评估结果、图表和实例模型是当前进程中的 ViewModel 数据，不写入新的评估报告格式。

## 参数与结果生命周期

模型定义位于 [`config/models/`](../config/models/)，训练参数和测试参数由 YAML 生成运行时参数模型，再在任务准备时写入对应数据库。测试参数按行为分为 `inference` 和 `evaluation`：

- 推理参数改变下一次推理输入或预测产物；需要用户重新启动测试任务。
- 评估参数只作用于已有预测结果；已有预测时可以重新评估，不重复推理。
- 任务运行期间参数编辑由 QML 禁用；任务停止、完成或失败后恢复。

完整语义以 [参数拆分访谈记录](GRILL_ME_EVALUATION_PARAMETER_SPLIT.md) 和 [异常检测可视化访谈记录](GRILL_ME_ANOMALY_SEGMENTATION_HEATMAP.md) 为准。

## 数据导出边界

模型任务需要的训练/测试文件由 `data` 模块导出，`model` 只负责任务准备和路径组织。外部格式导入导出属于数据模块的 `DataImporter`/`DataExporter` 扩展点；新增格式时同步转换逻辑、测试和对应的文件结构校验。
