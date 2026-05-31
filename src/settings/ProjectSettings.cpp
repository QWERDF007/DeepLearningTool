#include "settings/ProjectSettings.h"

#include "database/DataBase.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace dltool::settings {

ProjectSettings::ProjectSettings(QObject *parent)
    : QObject(parent)
{
}

ProjectSettings::~ProjectSettings() {}

void ProjectSettings::setMaxRecentProjects(int value)
{
    value = std::clamp(value, 1, 50);
    if (max_recent_projects_ != value)
    {
        max_recent_projects_ = value;
        emit maxRecentProjectsChanged();
    }
}

void ProjectSettings::setAutoSaveInterval(int value)
{
    value = std::max(30, value);
    if (auto_save_interval_ != value)
    {
        auto_save_interval_ = value;
        emit autoSaveIntervalChanged();
    }
}

void ProjectSettings::setAutoSaveEnabled(bool value)
{
    if (auto_save_enabled_ != value)
    {
        auto_save_enabled_ = value;
        emit autoSaveEnabledChanged();
    }
}

void ProjectSettings::load(database::SettingsDataBase *database)
{
    if (!database)
        return;

    QString err_msg;
    const auto row = database->loadProjectSettings(err_msg);
    if (!err_msg.isEmpty())
    {
        spdlog::warn("Load project settings failed: {}", err_msg.toUtf8().constData());
    }
    setMaxRecentProjects(row.value(QStringLiteral("max_recent_projects"), 10).toInt());
    setAutoSaveInterval(row.value(QStringLiteral("auto_save_interval"), 300).toInt());
    setAutoSaveEnabled(row.value(QStringLiteral("auto_save_enabled"), true).toBool());
}

void ProjectSettings::save(database::SettingsDataBase *database)
{
    if (!database)
        return;

    QString err_msg;
    database->saveProjectSettings(
        QVariantMap{
            {QStringLiteral("max_recent_projects"), max_recent_projects_},
            {QStringLiteral("auto_save_enabled"), auto_save_enabled_},
            {QStringLiteral("auto_save_interval"), auto_save_interval_},
        },
        err_msg);
    if (!err_msg.isEmpty())
        spdlog::error("Save project settings failed: {}", err_msg.toUtf8().constData());
}

void ProjectSettings::reset()
{
    setMaxRecentProjects(10);
    setAutoSaveInterval(300);
    setAutoSaveEnabled(true);
}

} // namespace dltool::settings
