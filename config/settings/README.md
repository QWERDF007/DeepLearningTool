# config/settings 配置说明

本目录是应用设置 schema 的配置源。`src/settings` 会在运行时读取这里的 `.yaml` / `.yml` 文件，生成设置目录、设置页字段模型、动态 QML 设置对象，并把设置值同步到 `settings.db`。

## 目录职责

- 每个 YAML 文件通常定义一个设置分组。
- 顶层 key 是分组名，例如 `DataSettings`、`ImageSearchSettings`。
- 分组通过 `accessor` 和可选的 `parent_accessor` 决定运行时访问路径。
- 字段通过 `property_name` 暴露为 QML 动态属性。
- 设置页等 UI 应优先从 catalog / accessor path 动态获取数据，避免在 QML 中硬编码分组名和字段结构。

## 分组字段

每个分组支持以下常用字段：

| 字段 | 说明 |
| --- | --- |
| `table` | 数据库表名。 |
| `accessor` | 当前分组在运行时对象树中的节点名。 |
| `parent_accessor` | 可选父级路径，例如 `advanced`。 |
| `category` | 设置分类，例如 `general`、`advanced`，用于 UI 分类或筛选。 |
| `ordinal_index` | 分组排序序号。 |
| `label` | 设置页显示名称。 |
| `sections` | 当前分组下的设置页分区。key 是分区标题，value 是该分区的字段列表。 |

运行时路径规则：

- `accessor: data` 对应 `data`。
- `accessor: ui` 对应 `ui`。
- `parent_accessor: advanced` + `accessor: imageSearch` 对应 `advanced.imageSearch`。

推荐 QML 访问方式：

```qml
property var dataSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.Data)
property var imageSearchSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.ImageSearch)

dataSettings.imageCellScale = 1.5
let enabled = imageSearchSettings.enabled
```

固定字段读写使用生成枚举：

```qml
GlobalSettings.valueForField(SettingsAccessor.Ui, UiField.Brightness, 0.0)
GlobalSettings.setFieldValue(SettingsAccessor.RoiSearch, RoiSearchField.TopK, 10)
```

## Section 与字段定义

`sections` 必须是 map。每个 section 节点作为字段列表父节点，同一个 section 的字段放在一起：

```yaml
sections:
  缩略图:
    - name_en: margin
      name_cn: 缩略图边距
      property_name: thumbnailMargin
      value: 10
      value_type: int
      control_type: slider
```

每个 section 下的字段条目支持以下常用字段：

| 字段 | 说明 |
| --- | --- |
| `name_en` | 持久化 key，数据库保存使用。 |
| `name_cn` | UI 显示名称。 |
| `property_name` | QML 动态属性名。 |
| `value` | 默认值，也是初始值。 |
| `default_value` | 可选重置值，未配置时使用 `value`。 |
| `value_type` | 值类型，例如 `bool`、`int`、`double`、`string`。 |
| `value_range` | 数值范围，格式为 `[from, to, step]`。 |
| `control_type` | 设置页控件类型，例如 `slider`、`spin`、`combo`、`checkbox`、`path`、`dir`、`folder`。 |
| `display_type` | 界面展示类型；未配置时回退到 `control_type`。 |
| `param_type` | 设置为 `dynamic` 时由动态 provider 提供选项。 |
| `backend_key` | 动态 provider 注册表中的接口 key。 |
| `options` | 静态选项列表。 |
| `options_values` | 可选的 combo 显示值到实际值映射；未配置时实际值等于 `options` 中的显示值。 |
| `options_map` | 动态选项映射。 |
| `options_key_field` | `options_map` 使用的键字段 `name_en`。 |
| `desc` | 配置项简短说明，用于设置页 tooltip。 |
| `description` | 可选说明文本。 |
| `visible` | 是否在设置页显示，默认 `true`。 |
| `ordinal_index` | 字段排序序号。 |
## 当前配置组

| 文件 | 分组 | 路径 | 分类 | 表 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `SoftwareSetting.yaml` | `SoftwareSetting` | `software` | `general` | `software_setting` | 软件设置，包括最近项目数量、自动保存间隔、自动保存开关、Python 环境目录。 |
| `DataSettings.yaml` | `DataSettings` | `data` | `general` | `data_settings` | 数据与缩略图显示设置，包括缩略图边距、缓存大小、图像网格缩放、标注缩略图缩放、边框宽度、填充透明度和导入轮廓拟合系数。 |
| `UISettings.yaml` | `UISettings` | `ui` | `general` | `ui_settings` | 界面设置，包括图像亮度、对比度、主题、语言。 |
| `ImageClusterSettings.yaml` | `ImageClusterSettings` | `advanced.imageCluster` | `advanced` | `image_cluster_settings` | 图像聚类配置，包括模型、特征层、推理运行时、精度、批量、PCA 和 HDBSCAN 参数。 |
| `RoiClusterSettings.yaml` | `RoiClusterSettings` | `advanced.roiCluster` | `advanced` | `roi_cluster_settings` | 标注 ROI 聚类配置，包括模型、ROIAlign、推理运行时、精度、PCA 和 HDBSCAN 参数。 |
| `ImageSearchSettings.yaml` | `ImageSearchSettings` | `advanced.imageSearch` | `advanced` | `image_search_settings` | 图像搜索配置，包括模型、权重路径、特征层、索引、推理运行时、精度、批量大小。 |
| `RoiSearchSettings.yaml` | `RoiSearchSettings` | `advanced.roiSearch` | `advanced` | `roi_search_settings` | ROI/标注搜索配置，在图像搜索基础上增加推理运行时、精度、ROI Align、PCA 等参数。 |
| `SmartAnnotationSettings.yaml` | `SmartAnnotationSettings` | `advanced.smartAnnotation` | `advanced` | `smart_annotation_settings` | 智能标注配置，包括 SAM 模型、权重路径、推理运行时、精度、mask 阈值、mask 透明度、刷新间隔。 |
| `FewShotLearningSettings.yaml` | `FewShotLearningSettings` | `advanced.fewShotLearning` | `advanced` | `few_shot_learning_settings` | 小样本学习配置，包括 SAM2 权重和训练参数。 |

## 新增设置约定

1. 新增分组时，优先新增一个 YAML 文件，配置 `table`、`accessor`、`label`、`category`、`ordinal_index` 和 `sections`。
2. 新增高级设置时，使用 `parent_accessor: advanced`，例如 `advanced.myFeature`。
3. QML/C++ 侧不要直接依赖文件名、group key 或属性名；固定字段优先使用生成的 `SettingsAccessor` 和 `*Field` 枚举。
4. 设置页字段展示应依赖 catalog 模型。
5. 每个字段应配置 `desc`，设置页会把它作为左侧文字区域的 tooltip。
6. 对于下拉选项，静态列表使用 `options`；需要显示值和保存值不一致时，使用 `options_values` 声明 key-value，例如 `options: [cpu, gpu]` 搭配 `options_values: {cpu: 0, gpu: 1}`。
7. 数值类字段建议配置 `value_range`，这样设置页和动态范围属性都能复用同一份 schema。

动态字段示例：

```yaml
- name_en: model_runtime
  name_cn: 模型运行时
  value_type: string
  param_type: dynamic
  display_type: combo
  backend_key: inferrt.compute_devices
```

动态 provider 由 `dltool_parameter` 的全局注册表管理；model、settings 和 feature 可以共享同一套 provider。provider 返回显示值和实际值，设置页面显示前者，数据库保存后者，例如 `NVIDIA GeForce RTX 4060 (tensorrt:0)` 的实际值为 `tensorrt:0`。
