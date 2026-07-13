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

} // namespace dltool::settings
