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
    // 验证：最近项目数量必须在 1-50 之间
    value = std::clamp(value, 1, 50);

    if (max_recent_projects_ != value)
    {
        max_recent_projects_ = value;
        emit maxRecentProjectsChanged();
    }
}

void ProjectSettings::setAutoSaveInterval(int value)
{
    // 验证：自动保存间隔必须大于等于 30 秒
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
    {
        return;
    }

    const QString group = QStringLiteral("Project");
    QString       err_msg;

    setMaxRecentProjects(database->value(group, QStringLiteral("maxRecentProjects"), 10, err_msg).toInt());
    setAutoSaveInterval(database->value(group, QStringLiteral("autoSaveInterval"), 300, err_msg).toInt());
    setAutoSaveEnabled(database->value(group, QStringLiteral("autoSaveEnabled"), true, err_msg).toBool());

    if (!err_msg.isEmpty())
    {
        spdlog::warn("Load Project settings failed: {}", err_msg.toUtf8().constData());
    }
}

void ProjectSettings::save(database::SettingsDataBase *database)
{
    if (!database)
    {
        return;
    }

    const QString group = QStringLiteral("Project");

    auto save_value = [database, &group](const QString &key, const QVariant &value) {
        QString err_msg;
        if (!database->setValue(group, key, value, err_msg))
        {
            spdlog::error("Save Project setting {} failed: {}", key.toUtf8().constData(), err_msg.toUtf8().constData());
        }
    };

    save_value(QStringLiteral("maxRecentProjects"), max_recent_projects_);
    save_value(QStringLiteral("autoSaveInterval"), auto_save_interval_);
    save_value(QStringLiteral("autoSaveEnabled"), auto_save_enabled_);
}

void ProjectSettings::reset()
{
    setMaxRecentProjects(10);
    setAutoSaveInterval(300);
    setAutoSaveEnabled(true);
}

} // namespace dltool::settings
