#pragma once

#include "dltool/settings/Export.h"

#include <QQmlPropertyMap>
#include <QtQml>

namespace dltool::settings {

class SettingsFieldModel;

class SETTINGS_API SettingsGroup : public QQmlPropertyMap
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString accessorPath READ accessorPath CONSTANT FINAL)
    Q_PROPERTY(QString groupKey READ groupKey CONSTANT FINAL)

public:
    explicit SettingsGroup(QObject *parent = nullptr);
    ~SettingsGroup() override;

    QString accessorPath() const;
    QString groupKey() const;

    SettingsFieldModel *fieldModel() const;
    void bindModel(QString accessor_path, SettingsFieldModel *model);
    void clearValues();
    void reloadFromModel();
    void updateFromFieldName(const QString &name, const QVariant &value);

    Q_INVOKABLE QVariant valueOr(const QString &property_name, const QVariant &fallback = {}) const;
    Q_INVOKABLE bool     setValue(const QString &property_name, const QVariant &value);

protected:
    QVariant updateValue(const QString &key, const QVariant &input) override;

private:
    void insertValue(const QString &key, const QVariant &value);
    void insertRangeValues(const QString &property_name, const QVariantList &range);

    QString             accessor_path_;
    SettingsFieldModel *field_model_{nullptr};
    bool                updating_{false};
};

class SETTINGS_API SettingsNamespace : public QQmlPropertyMap
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString accessorPath READ accessorPath CONSTANT FINAL)

public:
    explicit SettingsNamespace(QObject *parent = nullptr);
    ~SettingsNamespace() override;

    QString accessorPath() const;
    void setAccessorPath(QString accessor_path);
    void clearValues();
    void insertObject(const QString &accessor, QObject *object);
    Q_INVOKABLE QObject *object(const QString &accessor) const;

private:
    QString accessor_path_;
};

} // namespace dltool::settings
