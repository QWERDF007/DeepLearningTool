#include "settings/ProjectSettings.h"

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

void ProjectSettings::load(QSettings *settings)
{
    if (!settings)
    {
        return;
    }

    settings->beginGroup("Project");

    setMaxRecentProjects(settings->value("maxRecentProjects", 10).toInt());
    setAutoSaveInterval(settings->value("autoSaveInterval", 300).toInt());
    setAutoSaveEnabled(settings->value("autoSaveEnabled", true).toBool());

    settings->endGroup();
}

void ProjectSettings::save(QSettings *settings)
{
    if (!settings)
    {
        return;
    }

    settings->beginGroup("Project");

    settings->setValue("maxRecentProjects", max_recent_projects_);
    settings->setValue("autoSaveInterval", auto_save_interval_);
    settings->setValue("autoSaveEnabled", auto_save_enabled_);

    settings->endGroup();
}

void ProjectSettings::reset()
{
    setMaxRecentProjects(10);
    setAutoSaveInterval(300);
    setAutoSaveEnabled(true);
}

} // namespace dltool::settings
