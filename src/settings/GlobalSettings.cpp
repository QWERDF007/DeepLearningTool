#include "settings/GlobalSettings.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

namespace dltool::settings {

namespace {

QString settingsDatabasePath()
{
    return dltool::database::DataBase::applicationDatabasePath(QStringLiteral("settings.db"));
}

QStringList splitAccessorPath(const QString &accessor_path)
{
    return accessor_path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
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
    , project_settings_(new SettingsGroup(this))
    , data_settings_(new SettingsGroup(this))
    , advanced_settings_(new SettingsNamespace(this))
    , ui_settings_(new SettingsGroup(this))
    , settings_catalog_(new SettingsCatalog(this))
    , settings_database_(new dltool::database::SettingsDataBase(settingsDatabasePath(), this))
    , save_timer_(new QTimer(this))
{
    advanced_settings_->setAccessorPath(QStringLiteral("advanced"));
    namespaces_by_accessor_path_.insert(advanced_settings_->accessorPath(), advanced_settings_);

    save_timer_->setSingleShot(true);
    save_timer_->setInterval(1000);
    connect(save_timer_, &QTimer::timeout, this, &GlobalSettings::save);
    connectAutoSave();

    try
    {
        load();
        spdlog::info("Settings loaded successfully from: {}", settingsDatabasePath().toUtf8().constData());
    }
    catch (const std::exception &e)
    {
        spdlog::error("Failed to load settings: {}. Using default values.", e.what());
        reset();
    }
    catch (...)
    {
        spdlog::error("Unknown error occurred while loading settings. Using default values.");
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
            spdlog::info("Settings saved on application exit");
        }
        catch (const std::exception &e)
        {
            spdlog::error("Failed to save settings on exit: {}", e.what());
        }
        catch (...)
        {
            spdlog::error("Unknown error while saving settings on exit");
        }
    }
}

SettingsGroup *GlobalSettings::project() const
{
    return project_settings_;
}

SettingsGroup *GlobalSettings::data() const
{
    return data_settings_;
}

SettingsNamespace *GlobalSettings::advanced() const
{
    return advanced_settings_;
}

SettingsGroup *GlobalSettings::ui() const
{
    return ui_settings_;
}

SettingsCatalog *GlobalSettings::catalog() const
{
    return settings_catalog_;
}

void GlobalSettings::load()
{
    if (settings_database_ == nullptr)
    {
        spdlog::error("Cannot load settings: database object is null");
        return;
    }

    settings_catalog_->syncAndLoad(settings_database_);
    rebuildSettingsObjects();
    spdlog::info("All settings loaded successfully");
}

void GlobalSettings::save()
{
    if (settings_database_ == nullptr)
    {
        spdlog::error("Cannot save settings: database object is null");
        return;
    }

    settings_catalog_->save(settings_database_);
    spdlog::info("Settings saved successfully to: {}", settingsDatabasePath().toUtf8().constData());
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

QVariant GlobalSettings::value(const QString &accessor_path, const QString &property_name,
                               const QVariant &fallback) const
{
    const SettingsGroup *group = settingsGroup(accessor_path);
    return group != nullptr ? group->valueOr(property_name, fallback) : fallback;
}

bool GlobalSettings::setValue(const QString &accessor_path, const QString &property_name, const QVariant &value)
{
    SettingsGroup *group = settingsGroup(accessor_path);
    return group != nullptr && group->setValue(property_name, value);
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
    qDeleteAll(generated_groups_);
    generated_groups_.clear();

    qDeleteAll(generated_namespaces_);
    generated_namespaces_.clear();

    groups_by_accessor_path_.clear();
    groups_by_key_.clear();
    namespaces_by_accessor_path_.clear();

    project_settings_->bindModel(QString(), nullptr);
    data_settings_->bindModel(QString(), nullptr);
    ui_settings_->bindModel(QString(), nullptr);
    advanced_settings_->clearValues();
    advanced_settings_->setAccessorPath(QStringLiteral("advanced"));
    namespaces_by_accessor_path_.insert(advanced_settings_->accessorPath(), advanced_settings_);

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

        const QString parent_accessor = model->parentAccessor().trimmed();
        const QString accessor_path   = joinedAccessorPath(parent_accessor, accessor);
        SettingsGroup *group          = rootGroupForAccessor(accessor);
        if (group == nullptr || !parent_accessor.isEmpty())
        {
            group = new SettingsGroup(this);
            generated_groups_.append(group);
        }

        group->bindModel(accessor_path, model);
        groups_by_accessor_path_.insert(accessor_path, group);
        groups_by_key_.insert(model->groupKey(), group);

        if (!parent_accessor.isEmpty())
            ensureNamespace(parent_accessor)->insertObject(accessor, group);
    }
}

void GlobalSettings::handleCatalogValueChanged(const QString &group_key, const QString &name, const QVariant &value)
{
    SettingsGroup *group = groups_by_key_.value(group_key, nullptr);
    if (group != nullptr)
        group->updateFromFieldName(name, value);
    scheduleSave();
}

SettingsGroup *GlobalSettings::rootGroupForAccessor(const QString &accessor) const
{
    if (accessor == QStringLiteral("project"))
        return project_settings_;
    if (accessor == QStringLiteral("data"))
        return data_settings_;
    if (accessor == QStringLiteral("ui"))
        return ui_settings_;
    return nullptr;
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

    return settings_namespace;
}

QString GlobalSettings::joinedAccessorPath(const QString &parent_accessor, const QString &accessor)
{
    if (parent_accessor.isEmpty())
        return accessor;
    if (accessor.isEmpty())
        return parent_accessor;
    return parent_accessor + QLatin1Char('.') + accessor;
}

} // namespace dltool::settings
