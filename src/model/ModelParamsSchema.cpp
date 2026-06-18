#include "model/ModelParamsSchema.h"

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <string>
#include <utility>

namespace dltool::model {

namespace {

QString nodeString(const YAML::Node &node, const QString &fallback = {})
{
    if (!node || node.IsNull())
        return fallback;
    const std::string value = node.as<std::string>();
    return QString::fromUtf8(value.c_str());
}

QVariant nodeVariant(const YAML::Node &node)
{
    if (!node || node.IsNull())
        return {};
    if (node.IsScalar())
    {
        const QString text  = QString::fromUtf8(node.as<std::string>().c_str());
        const QString lower = text.toLower();
        if (lower == QStringLiteral("true"))
            return true;
        if (lower == QStringLiteral("false"))
            return false;

        bool ok = false;
        const qlonglong integer = text.toLongLong(&ok);
        if (ok)
            return integer;

        const double floating = text.toDouble(&ok);
        if (ok)
            return floating;

        return text;
    }
    if (node.IsSequence())
    {
        QVariantList list;
        for (const YAML::Node &entry : node)
            list.append(nodeVariant(entry));
        return list;
    }
    if (node.IsMap())
    {
        QVariantMap map;
        for (auto it = node.begin(); it != node.end(); ++it)
            map.insert(nodeString(it->first), nodeVariant(it->second));
        return map;
    }
    return {};
}

ParamDefinition parseParamDefinition(const YAML::Node &node)
{
    ParamDefinition param;
    if (!node || !node.IsMap())
        return param;

    param.name_en      = nodeString(node["name_en"]);
    param.name_cn      = nodeString(node["name_cn"], param.name_en);
    param.description  = nodeString(node["description"]);
    param.value        = nodeVariant(node["value"]);
    param.default_value = nodeVariant(node["default_value"]);
    if (!param.default_value.isValid())
        param.default_value = param.value;

    param.value_type   = nodeString(node["value_type"], QStringLiteral("string"));
    param.value_range  = nodeVariant(node["value_range"]).toList();
    param.control_type = nodeString(node["control_type"], QStringLiteral("text"));
    param.enabled      = node["enabled"] ? node["enabled"].as<bool>() : true;
    param.unit         = nodeString(node["unit"]);

    const YAML::Node options_node = node["options"];
    if (options_node && options_node.IsSequence())
    {
        for (const YAML::Node &entry : options_node)
            param.options.append(nodeString(entry));
    }

    return param;
}

ParamGroupDefinition parseParamGroupDefinition(const YAML::Node &node)
{
    ParamGroupDefinition group;
    if (!node || !node.IsMap())
        return group;

    group.name_en     = nodeString(node["name_en"]);
    group.name_cn     = nodeString(node["name_cn"], group.name_en);
    group.description = nodeString(node["description"]);
    group.enabled     = node["enabled"] ? node["enabled"].as<bool>() : true;
    group.part_index  = node["part_index"] ? node["part_index"].as<int>() : 0;

    const YAML::Node params_node = node["params"];
    if (params_node && params_node.IsSequence())
    {
        for (const YAML::Node &param_node : params_node)
        {
            ParamDefinition param = parseParamDefinition(param_node);
            if (!param.name_en.isEmpty())
                group.params.push_back(std::move(param));
        }
    }

    return group;
}

QFileInfo findModelConfigFile(const QString &type_name)
{
    const QDir config_dir(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config/models")));
    if (!config_dir.exists())
        return {};

    for (const QString &suffix : {QStringLiteral(".yaml"), QStringLiteral(".yml")})
    {
        const QFileInfo info(config_dir.filePath(type_name + suffix));
        if (info.exists() && info.isFile())
            return info;
    }
    return {};
}

bool looksLikeModelNode(const YAML::Node &node)
{
    return node && node.IsMap() && (node["method"] || node["train_params"] || node["test_params"]);
}

void parseGroups(const YAML::Node &groups_node, std::vector<ParamGroupDefinition> &groups)
{
    if (!groups_node || !groups_node.IsSequence())
        return;

    for (const YAML::Node &group_node : groups_node)
    {
        ParamGroupDefinition group = parseParamGroupDefinition(group_node);
        if (!group.name_en.isEmpty())
            groups.push_back(std::move(group));
    }
}

} // namespace

ModelParamsSchema loadModelParamsSchema(const QString &type_name)
{
    const QString trimmed_type_name = type_name.trimmed();
    ModelParamsSchema schema;
    schema.model_name = trimmed_type_name;

    if (trimmed_type_name.isEmpty())
        return schema;

    const QFileInfo config_file = findModelConfigFile(trimmed_type_name);
    if (!config_file.exists())
    {
        spdlog::warn("Model config file not found for: {}", trimmed_type_name.toUtf8().constData());
        return schema;
    }

    spdlog::info("Load model params schema: {}", config_file.absoluteFilePath().toUtf8().constData());

    try
    {
        const YAML::Node root = YAML::LoadFile(config_file.absoluteFilePath().toStdString());
        if (!root.IsMap())
        {
            spdlog::warn("Model config is not a map: {}", config_file.absoluteFilePath().toUtf8().constData());
            return schema;
        }

        const YAML::Node model_node = looksLikeModelNode(root) ? root : root[trimmed_type_name.toStdString()];
        if (!model_node || !model_node.IsMap())
        {
            spdlog::warn("Model config missing model '{}': {}", trimmed_type_name.toUtf8().constData(),
                         config_file.absoluteFilePath().toUtf8().constData());
            return schema;
        }

        schema.config_path = config_file.absoluteFilePath();
        schema.model_name  = nodeString(model_node["model_name"], trimmed_type_name).trimmed();
        if (schema.model_name.isEmpty())
            schema.model_name = trimmed_type_name;
        schema.method = nodeString(model_node["method"]);

        parseGroups(model_node["train_params"], schema.train_groups);
        parseGroups(model_node["test_params"], schema.test_groups);
    }
    catch (const std::exception &e)
    {
        spdlog::error("Failed to parse model config '{}': {}", config_file.absoluteFilePath().toUtf8().constData(),
                      e.what());
        schema.train_groups.clear();
        schema.test_groups.clear();
    }

    return schema;
}

} // namespace dltool::model
