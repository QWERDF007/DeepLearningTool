# settings 模块

`src/settings` 提供应用级设置的加载、运行时访问、QML 暴露和持久化能力。模块目标是让设置项完全由 `config/settings/*.yaml` 描述，C++ 不再为每一类设置维护固定参数类或硬编码参数名称。

## 模块边界

- CMake 目标为 `dltool_settings`，QML URI 为 `dltool.settings`。
- 依赖 Qt Core/QML、`dltool_common` 和 `dltool_database`；YAML 读写工具由 `dltool_common` 统一提供。
- YAML 配置只从应用程序目录下的 `config/settings` 读取，即 `QCoreApplication::applicationDirPath()/config/settings`。
- 设置数据库路径由 `DataBase::applicationDatabasePath("settings.db")` 生成。

## 核心文件

- `SettingsSchema.h/.cpp`：定义 YAML schema、字段模型、分组目录和数据库同步逻辑。
- `SettingsObjects.h/.cpp`：把字段模型转换为 QML 可直接访问的动态属性对象。
- `SettingsKeys.h/.cpp`：定义 QML 可用的 accessor/sidebar/field 枚举，并集中映射到 YAML 中的字符串 key。
- `GlobalSettings.h/.cpp`：QML 单例入口，负责加载、保存、重置和运行时对象树重建。

## YAML 到运行时模型

`SettingsCatalog` 通过 `dltool_common` 的 YAML 工具加载并解析应用目录 `config/settings` 下的 `.yaml` / `.yml` 文件。每个 YAML 顶层 group 会生成一个 `SettingsFieldModel`，并通过 catalog 暴露给设置页面。

每个 group 支持的主要元数据：

- `table`：对应的数据库表名；未配置时由 group 名自动转为 snake_case，并追加 `_settings`。
- `accessor`：运行时对象名，例如 `ui`、`data`、`project`。
- `parent_accessor` / `parent`：可选父级命名空间，例如 `advanced`。
- `category`：设置页面或其它界面可用于分类过滤的逻辑类别。
- `ordinal_index`：设置组排序值；未配置时按加载顺序生成。
- `sidebar`：可选的组级侧边栏或其它视图元数据。
- `label`：设置页面显示名称。
- `fields`：该组下的设置项列表。

每个 field 支持的主要字段：

- `name_en` / `key` / `name`：字段内部名称，也是数据库保存值时使用的 key。
- `name_cn` / `label`：界面显示文本。
- `property_name` / `property`：运行时 QML 属性名；未配置时回退到 `name_en`。
- `value` / `default_value`：当前默认值和重置值。
- `value_type` / `type`：值类型，支持 `bool`、`int`、`double`、`float`、`real`、`string` 等。
- `value_range` / `range`：数值范围，按 `[from, to, step]` 解释。
- `control_type` / `control`：界面控件类型，例如 `slider`、`combo`、`switch`、`path`。
- `options`：普通枚举选项。
- `options_map` / `key_values` / `values_map`：按其它字段值切换的动态选项。
- `sidebar`：字段级侧边栏元数据，按 sidebar key 分组，例如 `gallery`、`review`，可配置 `icon`、`ordinal_index`、`from`、`to`、`step`、`snap` 等。
- `section`、`description`、`visible`、`ordinal_index`：界面分组、说明、可见性和排序信息。

## QML 访问方式

`GlobalSettings` 是 QML 侧统一入口，不再暴露 `project`、`data`、`ui`、`advanced` 这类固定属性。运行时对象树由 YAML 的 `accessor` 和 `parent_accessor` 动态构建，并通过 `SettingsAccessor` 枚举访问：

```qml
readonly property var uiSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.Ui)
readonly property var dataSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.Data)
readonly property var imageSearchSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.ImageSearch)

uiSettings.imageBrightness
dataSettings.thumbnailMargin
imageSearchSettings.enabled
```

数值范围会自动扩展为动态属性：

```qml
uiSettings.imageBrightnessFrom
uiSettings.imageBrightnessTo
uiSettings.imageBrightnessStepSize
```

QML 侧优先使用枚举入口，避免手写 accessor path：

- `GlobalSettings.settingsObjectFor(SettingsAccessor.Ui)`
- `GlobalSettings.valueFor(SettingsAccessor.Data, propertyName, fallback)`
- `GlobalSettings.setValueFor(SettingsAccessor.Data, propertyName, value)`
- `GlobalSettings.catalog.optionsForAccessorKey(SettingsAccessor.ImageSearch, SettingsFieldKey.FeatureName, modelName)`
- `GlobalSettings.catalog.sidebarFieldsFor(SettingsSidebar.Gallery)`

底层仍保留字符串路径 API，主要用于配置驱动场景或 C++ 内部适配：

- `GlobalSettings.settingsObject(accessorPath)`
- `GlobalSettings.value(accessorPath, propertyName, fallback)`
- `GlobalSettings.setValue(accessorPath, propertyName, value)`
- `SettingsGroup.valueOr(propertyName, fallback)`
- `SettingsGroup.setValue(propertyName, value)`

## 动态对象树

`GlobalSettings::rebuildSettingsObjects()` 根据每个 group 的 `accessor` 和 `parent_accessor` 构建运行时对象树：

- `accessor: ui` 会创建路径为 `ui` 的 `SettingsGroup`。
- `accessor: data` 会创建路径为 `data` 的 `SettingsGroup`。
- `parent_accessor: advanced` + `accessor: imageSearch` 会创建路径为 `advanced.imageSearch` 的 `SettingsGroup`。
- `GlobalSettings.root` 是动态根命名空间，可通过 `root.object(name)` 取得一级对象；业务 QML 通常使用 `settingsObjectFor()`。

`SettingsGroup` 基于 `QQmlPropertyMap`，负责把字段值插入为动态属性。QML 直接改写动态属性时，`updateValue()` 会回写到对应的 `SettingsFieldModel`，再触发保存调度。

## 设置页面数据源

设置页面应以 `GlobalSettings.catalog` 作为 tab/group 数据源。每个 catalog 行提供：

- `groupKey`
- `tableName`
- `label`
- `accessor`
- `parentAccessor`
- `accessorPath`
- `category`
- `sidebar`
- `ordinalIndex`
- `fieldModel`

`fieldModel` 是字段列表模型，角色包括 `nameEn`、`nameCn`、`propertyName`、`value`、`defaultValue`、`valueType`、`valueRange`、`controlType`、`options`、`optionsMap`、`sidebar`、`section`、`description`、`visible`、`ordinalIndex`。界面应根据 `valueType` 和 `controlType` 创建对应控件，根据 `value` 赋值，根据 `valueRange` 设置范围。

## 数据库持久化

设置持久化由 `SettingsDataBase` 完成，底层继续走项目内的 sqlpp11 数据库封装，不在 settings 模块中直接操作 sqlite。

同步流程：

1. `GlobalSettings::load()` 调用 `SettingsCatalog::syncAndLoad()`。
2. catalog 首次加载 YAML schema。
3. 每个 group 以自己的 `table` 对应一张数据库表。
4. 表结构使用数据库模块中的统一 settings 表模板。
5. `syncSettingsSchema()` 同步 schema 行。
6. `loadSettings()` 读取已保存值并覆盖 YAML 默认值。

保存流程：

1. 字段值变化后，`SettingsFieldModel::valueChanged` 触发 `SettingsCatalog::fieldValueChanged`。
2. `GlobalSettings` 同步动态属性，并启动 1 秒单次自动保存定时器。
3. `GlobalSettings::save()` 调用 `SettingsCatalog::save()`。
4. 每个 group 通过 `saveSettings(tableName, valuesMap())` 写回数据库。

## 重置行为

`GlobalSettings::reset()` 会把所有字段恢复为 YAML 中的 `default_value`，如果没有单独配置 `default_value`，则使用 `value`。重置后会重新加载各 `SettingsGroup` 的动态属性，并调度自动保存。

`SettingsFieldModel::resetValues()` 会对实际变化的字段发出 `valueChanged`，用于同步 QML 控件和动态属性对象。

## 扩展设置项

新增或删除设置项时优先修改 `config/settings/*.yaml`：

1. 新增 group：添加一个 YAML 顶层 group，并配置 `table`、`accessor`、`label` 和 `fields`。
2. 新增字段：在 group 的 `fields` 下添加 field，并配置 `name_en`、`property_name`、`value_type`、`value`、`control_type` 等。
3. 动态列表：简单列表使用 `options`；如果列表依赖其它字段值，使用 `options_map` 这类 key-values 结构，例如按 `model` 映射到不同特征层名列表。
4. 访问路径：通过 `accessor` 和 `parent_accessor` 决定，例如 `parent_accessor: advanced` + `accessor: roiSearch` 对应内部路径 `advanced.roiSearch`，QML 使用 `SettingsAccessor.RoiSearch` 访问。
5. 侧边栏入口：给字段添加 `sidebar` 元数据，例如 `sidebar.gallery.icon: Brightness`，侧边栏通过 `GlobalSettings.catalog.sidebarFieldsFor(SettingsSidebar.Gallery)` 动态生成按钮。

只要 YAML schema、数据库模板和设置页面控件类型支持，对应设置无需新增 C++ 参数类。
