#pragma once

/**
 * @file SettingsSchema.h
 * @brief 设置配置模式、字段模型和分组目录模型声明。
 */

#include "dltool/settings/Export.h"
#include "settings/SettingsKeys.h"

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml>
#include <memory>
#include <vector>

namespace dltool::database {
class SettingsDataBase;
}

namespace dltool::settings {

/**
 * @brief 单个设置字段的元数据。
 * @details 对应配置文件中的一条字段定义，保存显示名、属性名、取值和界面控制信息。
 */
struct SETTINGS_API SettingsField
{
    QString      name_en;          ///< 英文键名，用于配置和代码侧索引字段。
    QString      name_cn;          ///< 中文显示名，用于界面展示。
    QString      property_name;    ///< 绑定到外部对象或 QML 的属性名.
    QVariant     value;            ///< 当前值.
    QVariant     default_value;    ///< 默认值.
    QString      value_type;       ///< 值类型，例如 string、bool、int、double.
    QVariantList value_range;      ///< 值域范围。
    QString      control_type;     ///< 控件类型，例如 text、slider、combo。
    QVariantList options;          ///< 选项列表。
    QVariantMap  options_map;      ///< 选项键值映射。
    QVariantMap  sidebar;          ///< 侧边栏展示配置。
    QString      section;          ///< 所属分区名称。
    QString      description;      ///< 说明文本。
    bool         visible{true};    ///< 是否在界面中显示。
    int          ordinal_index{0}; ///< 分组内排序索引。
};

/**
 * @brief 单个设置分组的字段列表模型。
 * @details 向 QML 暴露一组设置项及其分组元信息，支持按名称、属性名和侧边栏关键字访问。
 */
class SETTINGS_API SettingsFieldModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SettingsFieldModel)
    QML_UNCREATABLE("SettingsFieldModel is owned by SettingsCatalog")
    Q_PROPERTY(QString groupKey READ groupKey CONSTANT FINAL)
    Q_PROPERTY(QString tableName READ tableName CONSTANT FINAL)
    Q_PROPERTY(QString label READ label CONSTANT FINAL)
    Q_PROPERTY(QString accessor READ accessor CONSTANT FINAL)
    Q_PROPERTY(QString parentAccessor READ parentAccessor CONSTANT FINAL)
    Q_PROPERTY(QString accessorPath READ accessorPath CONSTANT FINAL)
    Q_PROPERTY(QString category READ category CONSTANT FINAL)
    Q_PROPERTY(QVariantMap sidebar READ sidebar CONSTANT FINAL)
    Q_PROPERTY(int ordinalIndex READ ordinalIndex CONSTANT FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    /**
     * @brief 模型角色定义。
     */
    enum Role
    {
        NameEnRole = Qt::UserRole + 1, ///< 英文键名。
        NameCnRole,                    ///< 中文名称。
        PropertyNameRole,              ///< 属性名。
        ValueRole,                     ///< 当前值。
        DefaultValueRole,              ///< 默认值。
        ValueTypeRole,                 ///< 值类型。
        ValueRangeRole,                ///< 值域范围。
        ControlTypeRole,               ///< 控件类型。
        OptionsRole,                   ///< 选项列表。
        OptionsMapRole,                ///< 选项映射。
        SidebarRole,                   ///< 侧边栏配置。
        SectionRole,                   ///< 分区名称。
        DescriptionRole,               ///< 描述文本。
        VisibleRole,                   ///< 可见性。
        OrdinalIndexRole,              ///< 排序索引。
    };
    Q_ENUM(Role)

    /**
     * @brief 创建一个空的设置分组模型。
     * @param parent QObject 父对象。
     */
    explicit SettingsFieldModel(QObject *parent = nullptr);

    /**
     * @brief 根据分组元信息和字段列表创建模型。
     * @param group_key 分组键。
     * @param table_name 设置表名。
     * @param label 分组显示名。
     * @param accessor 分组访问器。
     * @param parent_accessor 父级访问器。
     * @param category 分类名称。
     * @param sidebar 侧边栏配置。
     * @param ordinal_index 排序索引。
     * @param fields 字段列表。
     * @param parent QObject 父对象。
     */
    SettingsFieldModel(QString group_key, QString table_name, QString label, QString accessor, QString parent_accessor,
                       QString category, QVariantMap sidebar, int ordinal_index, std::vector<SettingsField> fields,
                       QObject *parent = nullptr);
    /**
     * @brief 销毁设置分组模型。
     */
    ~SettingsFieldModel() override;

    /**
     * @brief 获取分组键。
     * @return 分组键字符串。
     */
    QString groupKey() const;

    /**
     * @brief 获取设置表名。
     * @return 设置表名。
     */
    QString tableName() const;

    /**
     * @brief 获取分组显示名。
     * @return 分组显示名。
     */
    QString label() const;

    /**
     * @brief 获取分组访问器。
     * @return 分组访问器。
     */
    QString accessor() const;

    /**
     * @brief 获取父级访问器。
     * @return 父级访问器。
     */
    QString parentAccessor() const;

    /**
     * @brief 获取完整访问器路径。
     * @return 完整访问器路径。
     */
    QString accessorPath() const;

    /**
     * @brief 获取分类名称。
     * @return 分类名称。
     */
    QString category() const;

    /**
     * @brief 获取侧边栏配置。
     * @return 侧边栏配置映射。
     */
    QVariantMap sidebar() const;

    /**
     * @brief 获取排序索引。
     * @return 分组排序索引。
     */
    int ordinalIndex() const;

    /**
     * @brief 获取字段数量。
     * @return 字段数量。
     */
    int count() const;

    /**
     * @brief 返回模型行数。
     * @param parent 父索引。
     * @return 顶层字段行数；父索引有效时返回 0。
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * @brief 返回指定角色的数据。
     * @param index 模型索引。
     * @param role 数据角色。
     * @return 对应角色的数据；索引或角色无效时返回空 QVariant。
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /**
     * @brief 设置指定行的数据。
     * @param index 模型索引。
     * @param value 新值。
     * @param role 数据角色。
     * @return 设置成功返回 true，否则返回 false。
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    /**
     * @brief 返回条目标志。
     * @param index 模型索引。
     * @return Qt 条目标志。
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    /**
     * @brief 返回角色名称映射。
     * @return 角色到名称的映射。
     */
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 通过英文键名获取字段值。
     * @param name 字段英文键名。
     * @return 字段值；未找到时返回空 QVariant。
     */
    Q_INVOKABLE QVariant valueForName(const QString &name) const;

    /**
     * @brief 通过属性名获取字段值。
     * @param property_name 属性名。
     * @return 字段值；未找到时返回空 QVariant。
     */
    Q_INVOKABLE QVariant valueForProperty(const QString &property_name) const;

    /**
     * @brief 通过英文键名设置字段值。
     * @param name 字段英文键名。
     * @param value 新字段值。
     * @return 设置成功返回 true，否则返回 false。
     */
    Q_INVOKABLE bool setValueForName(const QString &name, const QVariant &value);

    /**
     * @brief 通过属性名设置字段值。
     * @param property_name 属性名。
     * @param value 新字段值。
     * @return 设置成功返回 true，否则返回 false。
     */
    Q_INVOKABLE bool setValueForProperty(const QString &property_name, const QVariant &value);

    /**
     * @brief 通过英文键名获取属性名。
     * @param name 字段英文键名。
     * @return 属性名；未找到时返回空字符串。
     */
    Q_INVOKABLE QString propertyForName(const QString &name) const;

    /**
     * @brief 通过属性名获取英文键名。
     * @param property_name 属性名。
     * @return 字段英文键名；未找到时返回空字符串。
     */
    Q_INVOKABLE QString nameForProperty(const QString &property_name) const;

    /**
     * @brief 获取指定行的字段字典。
     * @param row 字段行号。
     * @return 字段映射；行号无效时返回空映射。
     */
    Q_INVOKABLE QVariantMap fieldMap(int row) const;

    /**
     * @brief 获取某个字段在指定键下的选项列表。
     * @param name 字段英文键名。
     * @param key 选项映射键。
     * @return 选项列表；字段或键不存在时返回空列表。
     */
    Q_INVOKABLE QVariantList optionsForKey(const QString &name, const QString &key) const;

    /**
     * @brief 获取指定侧边栏关键字对应的字段列表。
     * @param sidebar_key 侧边栏关键字。
     * @return 侧边栏字段列表。
     */
    Q_INVOKABLE QVariantList sidebarFields(const QString &sidebar_key) const;

    /**
     * @brief 获取当前所有字段的值映射。
     * @return 字段英文键名到当前值的映射。
     */
    QVariantMap valuesMap() const;

    /**
     * @brief 获取当前 schema 的行数据。
     * @return 可同步到数据库的字段 schema 行列表。
     */
    QVariantList schemaRows() const;

    /**
     * @brief 从值映射加载字段值。
     * @param values 字段英文键名到值的映射。
     */
    void loadValues(const QVariantMap &values);

    /**
     * @brief 将所有字段恢复为默认值。
     */
    void resetValues();

signals:
    /**
     * @brief 当字段数量变化时触发。
     */
    void countChanged();

    /**
     * @brief 当某个字段值变化时触发。
     * @param name 字段英文键名。
     * @param value 新字段值。
     */
    void valueChanged(const QString &name, const QVariant &value);

private:
    /**
     * @brief 根据英文键名查找字段索引。
     * @param name 字段英文键名。
     * @return 找到时返回行号，否则返回 -1。
     */
    int indexOfName(const QString &name) const;

    /**
     * @brief 根据属性名查找字段索引。
     * @param property_name 属性名。
     * @return 找到时返回行号，否则返回 -1。
     */
    int indexOfProperty(const QString &property_name) const;

    /**
     * @brief 按字段类型转换值。
     * @param field 字段定义。
     * @param value 输入值。
     * @return 转换后的 QVariant 值。
     */
    QVariant typedValue(const SettingsField &field, const QVariant &value) const;

    /**
     * @brief 将字段转换为可序列化的 QVariantMap。
     * @param field 字段定义。
     * @return 字段映射。
     */
    QVariantMap toMap(const SettingsField &field) const;

    QString                    group_key_;
    QString                    table_name_;
    QString                    label_;
    QString                    accessor_;
    QString                    parent_accessor_;
    QString                    category_;
    QVariantMap                sidebar_;
    int                        ordinal_index_{0};
    std::vector<SettingsField> fields_;
};

/**
 * @brief 设置分组目录模型。
 * @details 聚合所有 SettingsFieldModel，提供按分组键、访问器和侧边栏关键字的查询接口。
 */
class SETTINGS_API SettingsCatalog : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SettingsCatalog)
    QML_UNCREATABLE("SettingsCatalog is owned by GlobalSettings")
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    /**
     * @brief 分组目录角色定义。
     */
    enum Role
    {
        GroupKeyRole = Qt::UserRole + 1, ///< 分组键。
        TableNameRole,                   ///< 设置表名。
        LabelRole,                       ///< 分组显示名。
        AccessorRole,                    ///< 分组访问器。
        ParentAccessorRole,              ///< 父级访问器。
        AccessorPathRole,                ///< 完整访问器路径。
        CategoryRole,                    ///< 分类名称。
        SidebarRole,                     ///< 侧边栏配置。
        OrdinalIndexRole,                ///< 排序索引。
        FieldModelRole,                  ///< 字段模型对象。
    };
    Q_ENUM(Role)

    /**
     * @brief 创建空的设置目录模型。
     * @param parent QObject 父对象。
     */
    explicit SettingsCatalog(QObject *parent = nullptr);

    /**
     * @brief 销毁设置目录模型。
     */
    ~SettingsCatalog() override;

    /**
     * @brief 获取分组数量。
     * @return 分组数量。
     */
    int count() const;

    /**
     * @brief 返回模型行数。
     * @param parent 父索引。
     * @return 顶层分组行数；父索引有效时返回 0。
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * @brief 返回指定角色的数据。
     * @param index 模型索引。
     * @param role 数据角色。
     * @return 对应角色的数据；索引或角色无效时返回空 QVariant。
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /**
     * @brief 返回角色名称映射。
     * @return 角色到名称的映射。
     */
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 通过分组键获取分组模型。
     * @param group_key 分组键。
     * @return 找到时返回 SettingsFieldModel 指针，否则返回 nullptr。
     */
    Q_INVOKABLE SettingsFieldModel *group(const QString &group_key) const;

    /**
     * @brief 通过行号获取分组模型。
     * @param row 分组行号。
     * @return 行号有效时返回 SettingsFieldModel 指针，否则返回 nullptr。
     */
    Q_INVOKABLE SettingsFieldModel *groupAt(int row) const;

    /**
     * @brief 通过访问器路径获取分组模型。
     * @param accessor_path 访问器路径。
     * @return 找到时返回 SettingsFieldModel 指针，否则返回 nullptr。
     */
    Q_INVOKABLE SettingsFieldModel *groupForAccessor(const QString &accessor_path) const;

    /**
     * @brief 读取某个分组字段的值。
     * @param group_key 分组键。
     * @param name 字段英文键名。
     * @param fallback 字段不存在时返回的备用值。
     * @return 字段值或备用值。
     */
    Q_INVOKABLE QVariant value(const QString &group_key, const QString &name, const QVariant &fallback = {}) const;

    /**
     * @brief 获取分组字段在指定键下的选项列表。
     * @param group_key 分组键。
     * @param name 字段英文键名。
     * @param key 选项映射键。
     * @return 选项列表；分组、字段或键不存在时返回空列表。
     */
    Q_INVOKABLE QVariantList optionsForKey(const QString &group_key, const QString &name, const QString &key) const;

    /**
     * @brief 通过访问器路径获取字段选项列表。
     * @param accessor_path 访问器路径。
     * @param name 字段英文键名。
     * @param key 选项映射键。
     * @return 选项列表；访问器、字段或键不存在时返回空列表。
     */
    Q_INVOKABLE QVariantList optionsForAccessor(const QString &accessor_path, const QString &name,
                                                const QString &key) const;

    /**
     * @brief 通过枚举键获取字段选项列表。
     * @param accessor_key SettingsAccessor 对应的整数键。
     * @param field_key SettingsFieldKey 对应的整数键。
     * @param key 选项映射键。
     * @return 选项列表；键无效或配置不存在时返回空列表。
     */
    Q_INVOKABLE QVariantList optionsForAccessorKey(int accessor_key, int field_key, const QString &key) const;

    /**
     * @brief 获取指定侧边栏关键字的字段列表。
     * @param sidebar_key 侧边栏关键字。
     * @return 所有分组中匹配该侧边栏的字段列表。
     */
    Q_INVOKABLE QVariantList sidebarFields(const QString &sidebar_key) const;

    /**
     * @brief 通过枚举键获取指定侧边栏关键字的字段列表。
     * @param sidebar_key SettingsSidebar 对应的整数键。
     * @return 所有分组中匹配该侧边栏的字段列表。
     */
    Q_INVOKABLE QVariantList sidebarFieldsFor(int sidebar_key) const;

    /**
     * @brief 从配置文件加载设置模式。
     * @param err_msg 加载失败时输出错误信息。
     * @return 加载成功返回 true，否则返回 false。
     */
    bool loadFromConfig(QString &err_msg);

    /**
     * @brief 同步数据库中的设置模式并加载当前值。
     * @param database 设置数据库指针。
     */
    void syncAndLoad(database::SettingsDataBase *database);

    /**
     * @brief 将当前设置值保存到数据库。
     * @param database 设置数据库指针。
     */
    void save(database::SettingsDataBase *database) const;

    /**
     * @brief 将所有字段恢复默认值。
     */
    void reset();

signals:
    /**
     * @brief 当分组数量变化时触发。
     */
    void countChanged();

    /**
     * @brief 当任意分组的值变化时触发。
     */
    void valueChanged();

    /**
     * @brief 当某个分组字段值变化时触发。
     * @param group_key 分组键。
     * @param name 字段英文键名。
     * @param value 新字段值。
     */
    void fieldValueChanged(const QString &group_key, const QString &name, const QVariant &value);

private:
    /**
     * @brief 新增或替换一个分组模型。
     * @param group_key 分组键。
     * @param table_name 设置表名。
     * @param label 分组显示名。
     * @param accessor 分组访问器。
     * @param parent_accessor 父级访问器。
     * @param category 分类名称。
     * @param sidebar 侧边栏配置。
     * @param ordinal_index 排序索引。
     * @param fields 字段列表。
     * @return 新增或替换后的 SettingsFieldModel 指针。
     */
    SettingsFieldModel *addGroup(QString group_key, QString table_name, QString label, QString accessor,
                                 QString parent_accessor, QString category, QVariantMap sidebar, int ordinal_index,
                                 std::vector<SettingsField> fields);

    /**
     * @brief 根据分组键查找分组索引。
     * @param group_key 分组键。
     * @return 找到时返回行号，否则返回 -1。
     */
    int indexOfGroup(const QString &group_key) const;

    /**
     * @brief 根据访问器路径查找分组索引。
     * @param accessor_path 访问器路径。
     * @return 找到时返回行号，否则返回 -1。
     */
    int indexOfAccessor(const QString &accessor_path) const;

    std::vector<std::unique_ptr<SettingsFieldModel>> groups_;
};

} // namespace dltool::settings
