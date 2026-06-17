#pragma once

#include "common/Singleton.h"
#include "dltool/settings/Export.h"
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

    Q_PROPERTY(SettingsGroup *project READ project CONSTANT FINAL)
    Q_PROPERTY(SettingsGroup *data READ data CONSTANT FINAL)
    Q_PROPERTY(SettingsNamespace *advanced READ advanced CONSTANT FINAL)
    Q_PROPERTY(SettingsGroup *ui READ ui CONSTANT FINAL)
    Q_PROPERTY(SettingsCatalog *catalog READ catalog CONSTANT FINAL)

public:
    SettingsGroup *project() const;
    SettingsGroup *data() const;
    SettingsNamespace *advanced() const;
    SettingsGroup *ui() const;
    SettingsCatalog *catalog() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void reset();
    Q_INVOKABLE void setAutoSaveEnabled(bool enabled);
    Q_INVOKABLE bool autoSaveEnabled() const;

    Q_INVOKABLE QObject *settingsObject(const QString &accessor_path) const;
    Q_INVOKABLE QVariant value(const QString &accessor_path, const QString &property_name,
                               const QVariant &fallback = {}) const;
    Q_INVOKABLE bool setValue(const QString &accessor_path, const QString &property_name, const QVariant &value);
    Q_INVOKABLE bool setCatalogValue(const QString &group_key, const QString &name, const QVariant &value);

    SettingsGroup *settingsGroup(const QString &accessor_path) const;

private:
    explicit GlobalSettings(QObject *parent = nullptr);
    ~GlobalSettings() override;

    void scheduleSave();
    void connectAutoSave();
    void rebuildSettingsObjects();
    void handleCatalogValueChanged(const QString &group_key, const QString &name, const QVariant &value);

    SettingsGroup *rootGroupForAccessor(const QString &accessor) const;
    SettingsNamespace *ensureNamespace(const QString &accessor_path);
    static QString joinedAccessorPath(const QString &parent_accessor, const QString &accessor);

    SettingsGroup     *project_settings_{nullptr};
    SettingsGroup     *data_settings_{nullptr};
    SettingsNamespace *advanced_settings_{nullptr};
    SettingsGroup     *ui_settings_{nullptr};
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
