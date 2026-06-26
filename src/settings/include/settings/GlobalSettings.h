#pragma once

/**
 * @file GlobalSettings.h
 * @brief 全局设置单例声明。
 */

#include "common/Singleton.h"
#include "dltool/settings/Export.h"
#include "settings/SettingsKeys.h"
#include "settings/SettingsObjects.h"
#include "settings/SettingsSchema.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QtQml>
#include <string_view>

namespace dltool::database {
class SettingsDataBase;
}

namespace dltool::settings {

/**
 * @brief 全局设置入口。
 * @details 负责加载设置 schema、同步数据库、构建 QML 设置对象树，并提供统一的读写和自动保存接口。
 */
class SETTINGS_API GlobalSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GlobalSettings)
    QT_QML_SINGLETON(GlobalSettings)

    Q_PROPERTY(SettingsNamespace *root READ root CONSTANT FINAL)
    Q_PROPERTY(SettingsCatalog *catalog READ catalog CONSTANT FINAL)

public:
    /**
     * @brief 获取根设置命名空间。
     * @return 根 SettingsNamespace 指针。
     */
    SettingsNamespace *root() const;

    /**
     * @brief 获取设置目录模型。
     * @return SettingsCatalog 指针。
     */
    SettingsCatalog *catalog() const;

    /**
     * @brief 从配置和数据库加载全局设置。
     */
    Q_INVOKABLE void load();

    /**
     * @brief 将当前设置保存到数据库。
     */
    Q_INVOKABLE void save();

    /**
     * @brief 将所有设置恢复为默认值。
     */
    Q_INVOKABLE void reset();

    /**
     * @brief 设置是否启用自动保存。
     * @param enabled true 表示启用自动保存，false 表示禁用。
     */
    Q_INVOKABLE void setAutoSaveEnabled(bool enabled);

    /**
     * @brief 查询是否启用自动保存。
     * @return 启用自动保存返回 true，否则返回 false。
     */
    Q_INVOKABLE bool autoSaveEnabled() const;

    /**
     * @brief 根据访问器枚举键获取设置对象。
     * @param accessor_key 生成 SettingsAccessor 对应的整数键。
     * @return 找到时返回 QObject 指针，否则返回 nullptr。
     */
    Q_INVOKABLE QObject *settingsObjectFor(int accessor_key) const;

    /**
     * @brief 通过访问器键和字段键读取属性值。
     * @param accessor_key 生成 SettingsAccessor 对应的整数键。
     * @param field_key 对应访问器的生成字段枚举整数值。
     * @param fallback 属性不存在时返回的备用值。
     * @return 属性值或备用值。
     */
    Q_INVOKABLE QVariant valueForField(int accessor_key, int field_key, const QVariant &fallback = {}) const;

    /**
     * @brief 读取指定生成字段的值域范围。
     * @param accessor_key 生成 SettingsAccessor 对应的整数键。
     * @param field_key 对应访问器的生成字段枚举整数值。
     * @return YAML value_range 列表；字段不存在时返回空列表。
     */
    Q_INVOKABLE QVariantList valueRangeForField(int accessor_key, int field_key) const;

    /**
     * @brief 通过生成设置访问器和字段英文键名读取字段值。
     * @param accessor_key 生成访问器键。
     * @param field_name 字段英文键名。
     * @param fallback 字段不存在时返回的备用值。
     * @return 字段值或备用值。
     */
    QVariant valueForGeneratedField(generated::AccessorKey accessor_key, std::string_view field_name,
                                    const QVariant &fallback = {}) const;

    /**
     * @brief 通过生成字段键读取字段值。
     * @param field_key 生成字段键，例如 generated::field::ImageSearch::ModelPath。
     * @param fallback 字段不存在时返回的备用值。
     * @return 字段值或备用值。
     */
    template<typename FieldKey>
    QVariant valueForField(FieldKey field_key, const QVariant &fallback = {}) const
    {
        return valueForGeneratedField(generated::accessorFor(field_key), generated::fieldName(field_key), fallback);
    }

    /**
     * @brief 通过生成设置访问器和字段英文键名设置字段值。
     * @param accessor_key 生成访问器键。
     * @param field_name 字段英文键名。
     * @param value 新字段值。
     * @return 设置成功返回 true，否则返回 false。
     */
    bool setGeneratedFieldValue(generated::AccessorKey accessor_key, std::string_view field_name,
                                const QVariant &value);

    /**
     * @brief 通过生成访问器和字段枚举设置字段值。
     * @param accessor_key 生成 SettingsAccessor 对应的整数键。
     * @param field_key 对应访问器的生成字段枚举整数值。
     * @param value 新字段值。
     * @return 设置成功返回 true，否则返回 false。
     */
    Q_INVOKABLE bool setFieldValue(int accessor_key, int field_key, const QVariant &value);

    /**
     * @brief 通过生成字段键设置字段值。
     * @param field_key 生成字段键，例如 generated::field::ImageSearch::FeatureName。
     * @param value 新字段值。
     * @return 设置成功返回 true，否则返回 false。
     */
    template<typename FieldKey>
    bool setFieldValue(FieldKey field_key, const QVariant &value)
    {
        return setGeneratedFieldValue(generated::accessorFor(field_key), generated::fieldName(field_key), value);
    }

    /**
     * @brief 设置目录模型中的字段值。
     * @param group_key 分组键。
     * @param name 字段英文键名。
     * @param value 新字段值。
     * @return 设置成功返回 true，否则返回 false。
     */
    Q_INVOKABLE bool setCatalogValue(const QString &group_key, const QString &name, const QVariant &value);

    /**
     * @brief 根据访问器路径获取设置分组对象。
     * @param accessor_path 访问器路径。
     * @return 找到时返回 SettingsGroup 指针，否则返回 nullptr。
     */
    SettingsGroup *settingsGroup(const QString &accessor_path) const;

    /**
     * @brief 根据生成访问器键获取设置分组对象。
     * @param accessor_key 生成访问器键。
     * @return 找到时返回 SettingsGroup 指针，否则返回 nullptr。
     */
    SettingsGroup *settingsGroup(generated::AccessorKey accessor_key) const;

private:
    /**
     * @brief 创建全局设置单例。
     * @param parent QObject 父对象。
     */
    explicit GlobalSettings(QObject *parent = nullptr);

    /**
     * @brief 销毁全局设置单例。
     */
    ~GlobalSettings() override;

    /**
     * @brief 安排一次延迟保存。
     */
    void scheduleSave();

    /**
     * @brief 建立字段变更到自动保存的连接。
     */
    void connectAutoSave();

    /**
     * @brief 根据设置目录重建 QML 设置对象树。
     */
    void rebuildSettingsObjects();

    /**
     * @brief 将自动保存相关配置应用到当前实例。
     */
    void applyAutoSaveSettings();

    /**
     * @brief 处理目录模型中字段值变化。
     * @param group_key 分组键。
     * @param name 字段英文键名。
     * @param value 新字段值。
     */
    void handleCatalogValueChanged(const QString &group_key, const QString &name, const QVariant &value);

    /**
     * @brief 根据访问器路径获取设置对象。
     * @param accessor_path 访问器路径，例如 advanced.imageSearch。
     * @return 找到时返回 QObject 指针，否则返回 nullptr。
     */
    QObject *settingsObjectByPath(const QString &accessor_path) const;

    /**
     * @brief 确保指定路径上的命名空间存在。
     * @param accessor_path 访问器路径。
     * @return 命名空间对象指针；创建失败时返回 nullptr。
     */
    SettingsNamespace *ensureNamespace(const QString &accessor_path);

    SettingsNamespace *root_settings_{nullptr};
    SettingsCatalog   *settings_catalog_{nullptr};

    QList<SettingsGroup *>              generated_groups_;
    QList<SettingsNamespace *>          generated_namespaces_;
    QHash<QString, SettingsGroup *>     groups_by_accessor_path_;
    QHash<QString, SettingsGroup *>     groups_by_key_;
    QHash<QString, SettingsNamespace *> namespaces_by_accessor_path_;

    database::SettingsDataBase *settings_database_{nullptr};
    QTimer                     *save_timer_{nullptr};
    bool                        auto_save_enabled_{true};
};

} // namespace dltool::settings
