#include "settings/GlobalSettings.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace dltool::settings {

namespace {

constexpr int kDefaultAutoSaveIntervalSeconds = 300;
constexpr int kMinAutoSaveIntervalSeconds     = 1;
constexpr int kMaxAutoSaveIntervalSeconds     = 24 * 60 * 60;

QString settingsDatabasePath()
{
    return dltool::database::DataBase::applicationDatabasePath(QStringLiteral("settings.db"));
}

QString parentAccessorPath(const QString &accessor_path)
{
    const qsizetype dot = accessor_path.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 ? accessor_path.left(dot) : QString();
}

QString leafAccessor(const QString &accessor_path)
{
    const qsizetype dot = accessor_path.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 ? accessor_path.mid(dot + 1) : accessor_path;
}

} // namespace

GlobalSettings::GlobalSettings(QObject *parent)
    : QObject(parent)
    , root_settings_(new SettingsNamespace(this))
    , settings_catalog_(new SettingsCatalog(this))
    , settings_database_(new dltool::database::SettingsDataBase(settingsDatabasePath(), this))
    , save_timer_(new QTimer(this))
{
    namespaces_by_accessor_path_.insert(QString(), root_settings_);

    save_timer_->setSingleShot(true);
    save_timer_->setInterval(1000);
    connect(save_timer_, &QTimer::timeout, this, &GlobalSettings::save);
    connectAutoSave();

    try
    {
        load();
        spdlog::info("成功加载设置: {}", settingsDatabasePath().toUtf8().constData());
    }
    catch (const std::exception &e)
    {
        spdlog::error("加载设置失败: {}. 使用默认值.", e.what());
        reset();
    }
    catch (...)
    {
        spdlog::error("加载设置时发生未知错误. 使用默认值.");
        reset();
    }
}

GlobalSettings::~GlobalSettings()
{
    if (auto_save_enabled_)
    {
        try
        {
            save();
            spdlog::info("应用退出时自动保存设置成功");
        }
        catch (const std::exception &e)
        {
            spdlog::error("自动保存设置时失败: {}", e.what());
        }
        catch (...)
        {
            spdlog::error("自动保存设置时发生未知错误");
        }
    }
}

SettingsNamespace *GlobalSettings::root() const
{
    return root_settings_;
}

SettingsCatalog *GlobalSettings::catalog() const
{
    return settings_catalog_;
}

void GlobalSettings::load()
{
    if (settings_database_ == nullptr)
    {
        spdlog::error("无法加载设置：数据库对象为空");
        return;
    }

    settings_catalog_->syncAndLoad(settings_database_);
    rebuildSettingsObjects();
    applyAutoSaveSettings();
    spdlog::info("所有设置加载成功");
}

void GlobalSettings::save()
{
    if (settings_database_ == nullptr)
    {
        spdlog::error("无法保持设置: 数据库对象为空");
        return;
    }

    settings_catalog_->save(settings_database_);
    spdlog::info("设置成功保存到: {}", settingsDatabasePath().toUtf8().constData());
}

void GlobalSettings::reset()
{
    settings_catalog_->reset();
    const QList<SettingsGroup *> groups = groups_by_accessor_path_.values();
    for (SettingsGroup *group : groups)
    {
        if (group != nullptr)
            group->reloadFromModel();
    }
    applyAutoSaveSettings();
    scheduleSave();
}

void GlobalSettings::setAutoSaveEnabled(const bool enabled)
{
    auto_save_enabled_ = enabled;
    spdlog::info("Auto-save {}", enabled ? "enabled" : "disabled");
}

bool GlobalSettings::autoSaveEnabled() const
{
    return auto_save_enabled_;
}

QObject *GlobalSettings::settingsObject(const QString &accessor_path) const
{
    if (SettingsGroup *group = settingsGroup(accessor_path); group != nullptr)
        return group;
    return namespaces_by_accessor_path_.value(accessor_path, nullptr);
}

QObject *GlobalSettings::settingsObjectFor(const int accessor_key) const
{
    return settingsObject(accessorPath(static_cast<accessor::Key>(accessor_key)));
}

QVariant GlobalSettings::value(const QString &accessor_path, const QString &property_name,
                               const QVariant &fallback) const
{
    const SettingsGroup *group = settingsGroup(accessor_path);
    return group != nullptr ? group->valueOr(property_name, fallback) : fallback;
}

QVariant GlobalSettings::valueFor(const int accessor_key, const QString &property_name, const QVariant &fallback) const
{
    return value(accessorPath(static_cast<accessor::Key>(accessor_key)), property_name, fallback);
}

QVariant GlobalSettings::valueForField(const int accessor_key, const int field_key, const QVariant &fallback) const
{
    const QString accessor_path = accessorPath(static_cast<accessor::Key>(accessor_key));
    const SettingsFieldModel *model = settings_catalog_ != nullptr ? settings_catalog_->groupForAccessor(accessor_path) : nullptr;
    const QString property_name = model != nullptr ? model->propertyForName(fieldName(static_cast<field::Key>(field_key))) : QString();
    return property_name.isEmpty() ? fallback : value(accessor_path, property_name, fallback);
}

bool GlobalSettings::setValue(const QString &accessor_path, const QString &property_name, const QVariant &value)
{
    SettingsGroup *group = settingsGroup(accessor_path);
    return group != nullptr && group->setValue(property_name, value);
}

bool GlobalSettings::setValueFor(const int accessor_key, const QString &property_name, const QVariant &value)
{
    return setValue(accessorPath(static_cast<accessor::Key>(accessor_key)), property_name, value);
}

bool GlobalSettings::setCatalogValue(const QString &group_key, const QString &name, const QVariant &value)
{
    SettingsFieldModel *model = settings_catalog_ != nullptr ? settings_catalog_->group(group_key) : nullptr;
    return model != nullptr && model->setValueForName(name, value);
}

SettingsGroup *GlobalSettings::settingsGroup(const QString &accessor_path) const
{
    return groups_by_accessor_path_.value(accessor_path, nullptr);
}

void GlobalSettings::scheduleSave()
{
    if (auto_save_enabled_ && save_timer_ != nullptr)
        save_timer_->start();
}

void GlobalSettings::connectAutoSave()
{
    connect(settings_catalog_, &SettingsCatalog::fieldValueChanged, this, &GlobalSettings::handleCatalogValueChanged);
}

void GlobalSettings::rebuildSettingsObjects()
{
    root_settings_->clearValues();

    qDeleteAll(generated_groups_);
    generated_groups_.clear();

    qDeleteAll(generated_namespaces_);
    generated_namespaces_.clear();

    groups_by_accessor_path_.clear();
    groups_by_key_.clear();
    namespaces_by_accessor_path_.clear();

    namespaces_by_accessor_path_.insert(QString(), root_settings_);

    for (int row = 0; row < settings_catalog_->rowCount(); ++row)
    {
        SettingsFieldModel *model = settings_catalog_->groupAt(row);
        if (model == nullptr)
            continue;

        const QString accessor = model->accessor().trimmed();
        if (accessor.isEmpty())
        {
            spdlog::warn("Skip settings group without accessor: {}", model->groupKey().toUtf8().constData());
            continue;
        }

        const QString accessor_path = model->accessorPath();
        SettingsGroup *group        = new SettingsGroup(this);
        generated_groups_.append(group);

        group->bindModel(accessor_path, model);
        groups_by_accessor_path_.insert(accessor_path, group);
        groups_by_key_.insert(model->groupKey(), group);

        ensureNamespace(parentAccessorPath(accessor_path))->insertObject(leafAccessor(accessor_path), group);
    }
}

void GlobalSettings::applyAutoSaveSettings()
{
    bool ok = false;
    int interval_seconds
        = valueForField(static_cast<int>(accessor::Key::Software), static_cast<int>(field::Key::AutoSaveInterval),
                        kDefaultAutoSaveIntervalSeconds)
              .toInt(&ok);
    if (!ok || interval_seconds <= 0)
        interval_seconds = kDefaultAutoSaveIntervalSeconds;

    interval_seconds = std::clamp(interval_seconds, kMinAutoSaveIntervalSeconds, kMaxAutoSaveIntervalSeconds);

    if (save_timer_ != nullptr)
        save_timer_->setInterval(interval_seconds * 1000);

    auto_save_enabled_ = valueForField(static_cast<int>(accessor::Key::Software),
                                       static_cast<int>(field::Key::AutoSaveEnabled), auto_save_enabled_)
                             .toBool();
}

void GlobalSettings::handleCatalogValueChanged(const QString &group_key, const QString &name, const QVariant &value)
{
    SettingsGroup *group = groups_by_key_.value(group_key, nullptr);
    if (group != nullptr)
        group->updateFromFieldName(name, value);

    const SettingsFieldModel *software_model
        = settings_catalog_ != nullptr ? settings_catalog_->groupForAccessor(accessorPath(accessor::Key::Software)) : nullptr;
    const bool auto_save_setting_changed
        = software_model != nullptr && group_key == software_model->groupKey()
          && (name == fieldName(field::Key::AutoSaveInterval) || name == fieldName(field::Key::AutoSaveEnabled));
    if (auto_save_setting_changed)
    {
        applyAutoSaveSettings();
        if (!auto_save_enabled_ || name == fieldName(field::Key::AutoSaveEnabled))
        {
            save();
            return;
        }
    }

    scheduleSave();
}

SettingsNamespace *GlobalSettings::ensureNamespace(const QString &accessor_path)
{
    if (SettingsNamespace *existing = namespaces_by_accessor_path_.value(accessor_path, nullptr); existing != nullptr)
        return existing;

    SettingsNamespace *settings_namespace = new SettingsNamespace(this);
    settings_namespace->setAccessorPath(accessor_path);
    generated_namespaces_.append(settings_namespace);
    namespaces_by_accessor_path_.insert(accessor_path, settings_namespace);

    const QString parent_path = parentAccessorPath(accessor_path);
    if (!parent_path.isEmpty())
        ensureNamespace(parent_path)->insertObject(leafAccessor(accessor_path), settings_namespace);
    else
        root_settings_->insertObject(leafAccessor(accessor_path), settings_namespace);

    return settings_namespace;
}

} // namespace dltool::settings
