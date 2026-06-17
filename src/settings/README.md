# settings 模块

`src/settings` 提供应用级设置的加载、运行时访问、QML 暴露和持久化能力。模块目标是让设置项完全由 `config/settings/*.yaml` 描述，C++ 不再为每一类设置维护固定参数类或硬编码参数名称。

## 模块边界

- CMake 目标为 `dltool_settings`，QML URI 为 `dltool.settings`。
- 依赖 Qt Core/QML、`dltool_common`、`dltool_database` 和 `yaml-cpp`。
- YAML 配置只从应用程序目录下的 `config/settings` 读取，即 `QCoreApplication::applicationDirPath()/config/settings`。
- 设置数据库路径由 `DataBase::applicationDatabasePath("settings.db")` 生成。

## 核心文件

- `SettingsSchema.h/.cpp`：定义 YAML schema、字段模型、分组目录和数据库同步逻辑。
- `SettingsObjects.h/.cpp`：把字段模型转换为 QML 可直接访问的动态属性对象。
- `GlobalSettings.h/.cpp`：QML 单例入口，负责加载、保存、重置和运行时对象树重建。

## YAML 到运行时模型

`SettingsCatalog` 使用 `yaml-cpp` 解析应用目录 `config/settings` 下的 `.yaml` / `.yml` 文件。每个 YAML 顶层 group 会生成一个 `SettingsFieldModel`，并通过 catalog 暴露给设置页面。

每个 group 支持的主要元数据：

- `table`：对应的数据库表名；未配置时由 group 名自动转为 snake_case，并追加 `_settings`。
- `accessor`：运行时对象名，例如 `ui`、`data`、`project`。
- `parent_accessor` / `parent`：可选父级命名空间，例如 `advanced`。
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
- `section`、`description`、`visible`、`ordinal_index`：界面分组、说明、可见性和排序信息。

## QML 访问方式

`GlobalSettings` 是 QML 侧统一入口，暴露以下固定根节点：

- `GlobalSettings.project`
- `GlobalSettings.data`
- `GlobalSettings.ui`
- `GlobalSettings.advanced`
- `GlobalSettings.catalog`

`project`、`data`、`ui` 是 `SettingsGroup`，`advanced` 是 `SettingsNamespace`。具体属性由 YAML 中每个 field 的 `property_name` 动态插入。例如：

```qml
GlobalSettings.ui.imageBrightness
GlobalSettings.data.thumbnailMargin
GlobalSettings.advanced.imageSearch.enabled
```

数值范围会自动扩展为动态属性：

```qml
GlobalSettings.ui.imageBrightnessFrom
GlobalSettings.ui.imageBrightnessTo
GlobalSettings.ui.imageBrightnessStepSize
```

如果需要从 C++/QML 通过字符串路径访问，可以使用：

- `GlobalSettings.value(accessorPath, propertyName, fallback)`
- `GlobalSettings.setValue(accessorPath, propertyName, value)`
- `GlobalSettings.settingsObject(accessorPath)`
- `SettingsGroup.valueOr(propertyName, fallback)`
- `SettingsGroup.setValue(propertyName, value)`

## 动态对象树

`GlobalSettings::rebuildSettingsObjects()` 根据每个 group 的 `accessor` 和 `parent_accessor` 构建运行时对象树：

- `accessor: ui` 绑定到固定根对象 `GlobalSettings.ui`。
- `accessor: data` 绑定到固定根对象 `GlobalSettings.data`。
- `accessor: project` 绑定到固定根对象 `GlobalSettings.project`。
- 带 `parent_accessor: advanced` 的 group 会创建在 `GlobalSettings.advanced.<accessor>` 下。

`SettingsGroup` 基于 `QQmlPropertyMap`，负责把字段值插入为动态属性。QML 直接改写动态属性时，`updateValue()` 会回写到对应的 `SettingsFieldModel`，再触发保存调度。

## 设置页面数据源

设置页面应以 `GlobalSettings.catalog` 作为 tab/group 数据源。每个 catalog 行提供：

- `groupKey`
- `tableName`
- `label`
- `fieldModel`

`fieldModel` 是字段列表模型，角色包括 `nameEn`、`nameCn`、`propertyName`、`value`、`defaultValue`、`valueType`、`valueRange`、`controlType`、`options`、`optionsMap`、`section`、`description`、`visible`、`ordinalIndex`。界面应根据 `valueType` 和 `controlType` 创建对应控件，根据 `value` 赋值，根据 `valueRange` 设置范围。

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
4. 访问路径：通过 `accessor` 和 `parent_accessor` 决定，例如 `parent_accessor: advanced` + `accessor: roiSearch` 对应 `GlobalSettings.advanced.roiSearch`。

只要 YAML schema、数据库模板和设置页面控件类型支持，对应设置无需新增 C++ 参数类。
