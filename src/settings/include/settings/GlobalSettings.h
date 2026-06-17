#pragma once

#include "common/Singleton.h"
#include "dltool/settings/Export.h"
#include "settings/SettingsKeys.h"
#include "settings/SettingsObjects.h"
#include "settings/SettingsSchema.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QtQml>

namespace dltool::database {
class SettingsDataBase;
}

namespace dltool::settings {

class SETTINGS_API GlobalSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GlobalSettings)
    QT_QML_SINGLETON(GlobalSettings)

    Q_PROPERTY(SettingsNamespace *root READ root CONSTANT FINAL)
    Q_PROPERTY(SettingsCatalog *catalog READ catalog CONSTANT FINAL)

public:
    SettingsNamespace *root() const;
    SettingsCatalog *catalog() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void setAutoSaveEnabled(bool enabled);
    Q_INVOKABLE bool autoSaveEnabled() const;

    Q_INVOKABLE QObject *settingsObject(const QString &accessor_path) const;
    Q_INVOKABLE QObject *settingsObjectFor(int accessor_key) const;
    Q_INVOKABLE QVariant value(const QString &accessor_path, const QString &property_name,
                               const QVariant &fallback = {}) const;
    Q_INVOKABLE QVariant valueFor(int accessor_key, const QString &property_name,
                                  const QVariant &fallback = {}) const;
    Q_INVOKABLE bool setValue(const QString &accessor_path, const QString &property_name, const QVariant &value);
    Q_INVOKABLE bool setValueFor(int accessor_key, const QString &property_name, const QVariant &value);
    Q_INVOKABLE bool setCatalogValue(const QString &group_key, const QString &name, const QVariant &value);

    SettingsGroup *settingsGroup(const QString &accessor_path) const;

private:
    explicit GlobalSettings(QObject *parent = nullptr);
    ~GlobalSettings() override;

    void scheduleSave();
    void connectAutoSave();
    void rebuildSettingsObjects();
    void handleCatalogValueChanged(const QString &group_key, const QString &name, const QVariant &value);

    SettingsNamespace *ensureNamespace(const QString &accessor_path);

    SettingsNamespace *root_settings_{nullptr};
    SettingsCatalog   *settings_catalog_{nullptr};

    QList<SettingsGroup *>     generated_groups_;
    QList<SettingsNamespace *> generated_namespaces_;
    QHash<QString, SettingsGroup *> groups_by_accessor_path_;
    QHash<QString, SettingsGroup *> groups_by_key_;
    QHash<QString, SettingsNamespace *> namespaces_by_accessor_path_;

    database::SettingsDataBase *settings_database_{nullptr};
    QTimer *save_timer_{nullptr};
    bool auto_save_enabled_{true};
};

} // namespace dltool::settings
