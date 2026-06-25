#pragma once

/**
 * @file SettingsObjects.h
 * @brief QML 可访问的设置对象和设置命名空间声明。
 */

#include "dltool/settings/Export.h"

#include <QQmlPropertyMap>
#include <QtQml>

namespace dltool::settings {

class SettingsFieldModel;

/**
 * @brief 单个设置分组的 QML 属性映射对象。
 * @details 将 SettingsFieldModel 中的字段值暴露为 QQmlPropertyMap，供 QML 通过属性访问和修改。
 */
class SETTINGS_API SettingsGroup : public QQmlPropertyMap
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString accessorPath READ accessorPath CONSTANT FINAL)
    Q_PROPERTY(QString groupKey READ groupKey CONSTANT FINAL)
    Q_PROPERTY(dltool::settings::SettingsFieldModel *fieldModel READ fieldModel CONSTANT FINAL)

public:
    /**
     * @brief 创建空的设置分组对象。
     * @param parent QObject 父对象。
     */
    explicit SettingsGroup(QObject *parent = nullptr);

    /**
     * @brief 销毁设置分组对象。
     */
    ~SettingsGroup() override;

    /**
     * @brief 获取完整访问器路径。
     * @return 当前分组的访问器路径。
     */
    QString accessorPath() const;

    /**
     * @brief 获取绑定的分组键。
     * @return 当前分组键；未绑定模型时返回空字符串。
     */
    QString groupKey() const;

    /**
     * @brief 获取绑定的字段模型。
     * @return 字段模型指针；未绑定时返回 nullptr。
     */
    SettingsFieldModel *fieldModel() const;

    /**
     * @brief 绑定字段模型并重载属性值。
     * @param accessor_path 当前分组的完整访问器路径。
     * @param model 需要绑定的字段模型。
     */
    void bindModel(QString accessor_path, SettingsFieldModel *model);

    /**
     * @brief 清空当前属性映射中的所有值。
     */
    void clearValues();

    /**
     * @brief 从已绑定字段模型重新加载所有属性值。
     */
    void reloadFromModel();

    /**
     * @brief 根据字段英文键名更新对应属性值。
     * @param name 字段英文键名。
     * @param value 新字段值。
     */
    void updateFromFieldName(const QString &name, const QVariant &value);

    /**
     * @brief 获取属性值，属性不存在时返回备用值。
     * @param property_name 属性名。
     * @param fallback 属性不存在或值无效时返回的备用值。
     * @return 属性值或备用值。
     */
    Q_INVOKABLE QVariant valueOr(const QString &property_name, const QVariant &fallback = {}) const;

    /**
     * @brief 设置指定属性的值。
     * @param property_name 属性名。
     * @param value 新属性值。
     * @return 设置成功返回 true，否则返回 false。
     */
    Q_INVOKABLE bool setValue(const QString &property_name, const QVariant &value);

protected:
    /**
     * @brief QQmlPropertyMap 属性更新回调。
     * @param key 被修改的属性名。
     * @param input 输入值。
     * @return 实际写入的值。
     */
    QVariant updateValue(const QString &key, const QVariant &input) override;

private:
    /**
     * @brief 向属性映射插入单个值。
     * @param key 属性名。
     * @param value 属性值。
     */
    void insertValue(const QString &key, const QVariant &value);

    /**
     * @brief 为范围字段插入 from、to、step 等派生属性。
     * @param property_name 字段属性名。
     * @param range 范围定义列表。
     */
    void insertRangeValues(const QString &property_name, const QVariantList &range);

    QString             accessor_path_;
    SettingsFieldModel *field_model_{nullptr};
    bool                updating_{false};
};

/**
 * @brief 设置命名空间对象。
 * @details 通过 QQmlPropertyMap 挂载 SettingsGroup 或下级 SettingsNamespace，形成 QML 可访问的设置对象树。
 */
class SETTINGS_API SettingsNamespace : public QQmlPropertyMap
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString accessorPath READ accessorPath CONSTANT FINAL)

public:
    /**
     * @brief 创建设置命名空间对象。
     * @param parent QObject 父对象。
     */
    explicit SettingsNamespace(QObject *parent = nullptr);

    /**
     * @brief 销毁设置命名空间对象。
     */
    ~SettingsNamespace() override;

    /**
     * @brief 获取命名空间访问器路径。
     * @return 访问器路径。
     */
    QString accessorPath() const;

    /**
     * @brief 设置命名空间访问器路径。
     * @param accessor_path 访问器路径。
     */
    void setAccessorPath(QString accessor_path);

    /**
     * @brief 清空当前命名空间下挂载的所有属性。
     */
    void clearValues();

    /**
     * @brief 插入子对象。
     * @param accessor 子访问器名。
     * @param object 要挂载的 QObject 对象。
     */
    void insertObject(const QString &accessor, QObject *object);

    /**
     * @brief 获取指定子访问器对应的对象。
     * @param accessor 子访问器名。
     * @return 找到时返回 QObject 指针，否则返回 nullptr。
     */
    Q_INVOKABLE QObject *object(const QString &accessor) const;

private:
    QString accessor_path_;
};

} // namespace dltool::settings
