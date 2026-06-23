#include "settings/SettingsKeys.h"

namespace dltool::settings {

QString accessorPath(const accessor::Key key)
{
    switch (key)
    {
    case accessor::Key::Software:
        return QStringLiteral("software");
    case accessor::Key::Data:
        return QStringLiteral("data");
    case accessor::Key::Ui:
        return QStringLiteral("ui");
    case accessor::Key::ImageSearch:
        return QStringLiteral("advanced.imageSearch");
    case accessor::Key::RoiSearch:
        return QStringLiteral("advanced.roiSearch");
    case accessor::Key::SmartAnnotation:
        return QStringLiteral("advanced.smartAnnotation");
    }
    return {};
}

QString accessorPath(const int key)
{
    return accessorPath(static_cast<accessor::Key>(key));
}

QString fieldName(const field::Key key)
{
    switch (key)
    {
    case field::Key::Model:
        return QStringLiteral("model");
    case field::Key::FeatureName:
        return QStringLiteral("feature_name");
    case field::Key::MaxRecentProjects:
        return QStringLiteral("max_recent_projects");
    case field::Key::AutoSaveInterval:
        return QStringLiteral("auto_save_interval");
    case field::Key::AutoSaveEnabled:
        return QStringLiteral("auto_save_enabled");
    case field::Key::PythonEnvPath:
        return QStringLiteral("python_env_path");
    }
    return {};
}

QString fieldName(const int key)
{
    return fieldName(static_cast<field::Key>(key));
}

QString sidebarName(const sidebar::Key key)
{
    switch (key)
    {
    case sidebar::Key::Gallery:
        return QStringLiteral("gallery");
    case sidebar::Key::Review:
        return QStringLiteral("review");
    }
    return {};
}

QString sidebarName(const int key)
{
    return sidebarName(static_cast<sidebar::Key>(key));
}

} // namespace dltool::settings
