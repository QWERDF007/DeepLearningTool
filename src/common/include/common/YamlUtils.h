#pragma once

#include "dltool/common/Export.h"

#include <yaml-cpp/yaml.h>

#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <string>

namespace dltool::common::yaml {

COMMON_API QString  nodeString(const YAML::Node &node, const QString &fallback = {});
COMMON_API QVariant nodeVariant(const YAML::Node &node);
COMMON_API std::string toYamlString(const QString &value);
COMMON_API YAML::Node scalarNode(const QString &value);
COMMON_API YAML::Node variantToYaml(const QVariant &value);
COMMON_API void       setMapValue(YAML::Node &node, const QString &key, const YAML::Node &value);
COMMON_API void       setMapValue(YAML::Node &node, const QString &key, const QString &value);
COMMON_API void       setMapValue(YAML::Node &node, const QString &key, int value);
COMMON_API void       setMapValue(YAML::Node &node, const QString &key, qint64 value);
COMMON_API void       setMapValue(YAML::Node &node, const QString &key, bool value);

COMMON_API YAML::Node loadFile(const QFileInfo &file);
COMMON_API bool       writeFile(const QString &path, const YAML::Node &node, QString *err_msg = nullptr,
                                const QString &open_error_prefix = {}, const QString &emit_error_prefix = {});
COMMON_API QFileInfo  findConfigFile(const QString &directory, const QString &base_name,
                                     const QStringList &suffixes = {QStringLiteral(".yaml"), QStringLiteral(".yml")});
COMMON_API QVector<QFileInfo> configFiles(const QStringList &directories,
                                          const QStringList &name_filters
                                          = {QStringLiteral("*.yaml"), QStringLiteral("*.yml")});

} // namespace dltool::common::yaml
