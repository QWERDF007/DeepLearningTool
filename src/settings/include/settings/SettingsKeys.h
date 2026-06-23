#pragma once

#include "dltool/settings/Export.h"

#include <QString>
#include <QtQml>

namespace dltool::settings::accessor {
Q_NAMESPACE_EXPORT(SETTINGS_API)

enum class Key
{
    Software = 0,
    Data,
    Ui,
    ImageSearch,
    RoiSearch,
    SmartAnnotation,
};
Q_ENUM_NS(Key)
QML_NAMED_ELEMENT(SettingsAccessor)
} // namespace dltool::settings::accessor

namespace dltool::settings::field {
Q_NAMESPACE_EXPORT(SETTINGS_API)

enum class Key
{
    Model = 0,
    FeatureName,
    MaxRecentProjects,
    AutoSaveInterval,
    AutoSaveEnabled,
    PythonEnvPath,
};
Q_ENUM_NS(Key)
QML_NAMED_ELEMENT(SettingsFieldKey)
} // namespace dltool::settings::field

namespace dltool::settings::sidebar {
Q_NAMESPACE_EXPORT(SETTINGS_API)

enum class Key
{
    Gallery = 0,
    Review,
};
Q_ENUM_NS(Key)
QML_NAMED_ELEMENT(SettingsSidebar)
} // namespace dltool::settings::sidebar

namespace dltool::settings {

SETTINGS_API QString accessorPath(accessor::Key key);
SETTINGS_API QString accessorPath(int key);
SETTINGS_API QString fieldName(field::Key key);
SETTINGS_API QString fieldName(int key);
SETTINGS_API QString sidebarName(sidebar::Key key);
SETTINGS_API QString sidebarName(int key);

} // namespace dltool::settings
