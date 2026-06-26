#include "settings/SettingsKeys.h"

namespace dltool::settings {

QString toQString(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString fieldName(const generated::AccessorKey accessor_key, const int field_key)
{
    return toQString(generated::fieldName(accessor_key, field_key));
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
