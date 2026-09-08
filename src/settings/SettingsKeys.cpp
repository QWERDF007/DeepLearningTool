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

std::optional<generated::AccessorKey> accessorKeyForPath(const QString &path)
{
    if (path == QStringLiteral("data"))
        return generated::AccessorKey::Data;
    if (path == QStringLiteral("advanced.fewShotLearning"))
        return generated::AccessorKey::FewShotLearning;
    if (path == QStringLiteral("advanced.imageCluster"))
        return generated::AccessorKey::ImageCluster;
    if (path == QStringLiteral("advanced.imageSearch"))
        return generated::AccessorKey::ImageSearch;
    if (path == QStringLiteral("advanced.roiCluster"))
        return generated::AccessorKey::RoiCluster;
    if (path == QStringLiteral("advanced.roiSearch"))
        return generated::AccessorKey::RoiSearch;
    if (path == QStringLiteral("advanced.smartAnnotation"))
        return generated::AccessorKey::SmartAnnotation;
    if (path == QStringLiteral("software"))
        return generated::AccessorKey::Software;
    if (path == QStringLiteral("ui"))
        return generated::AccessorKey::Ui;
    return std::nullopt;
}

std::optional<generated::AccessorKey> accessorKeyForGroupKey(const QString &group_key)
{
    if (group_key == QStringLiteral("DataSettings"))
        return generated::AccessorKey::Data;
    if (group_key == QStringLiteral("FewShotLearningSettings"))
        return generated::AccessorKey::FewShotLearning;
    if (group_key == QStringLiteral("ImageClusterSettings"))
        return generated::AccessorKey::ImageCluster;
    if (group_key == QStringLiteral("ImageSearchSettings"))
        return generated::AccessorKey::ImageSearch;
    if (group_key == QStringLiteral("RoiClusterSettings"))
        return generated::AccessorKey::RoiCluster;
    if (group_key == QStringLiteral("RoiSearchSettings"))
        return generated::AccessorKey::RoiSearch;
    if (group_key == QStringLiteral("SmartAnnotationSettings"))
        return generated::AccessorKey::SmartAnnotation;
    if (group_key == QStringLiteral("SoftwareSetting"))
        return generated::AccessorKey::Software;
    if (group_key == QStringLiteral("UISettings"))
        return generated::AccessorKey::Ui;
    return std::nullopt;
}

} // namespace dltool::settings
