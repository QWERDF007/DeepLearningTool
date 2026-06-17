#include "model/ModelParamsSchema.h"

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <initializer_list>
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

YAML::Node firstNode(const YAML::Node &node, std::initializer_list<const char *> keys)
{
    for (const char *key : keys)
    {
        const YAML::Node value = node[key];
        if (value)
            return value;
    }
    return {};
}

ParamEditorType editorTypeFromString(const QString &text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("integer"))
        return ParamEditorType::Integer;
    if (normalized == QStringLiteral("double"))
        return ParamEditorType::Double;
    if (normalized == QStringLiteral("slider"))
        return ParamEditorType::Slider;
    if (normalized == QStringLiteral("checkbox"))
        return ParamEditorType::CheckBox;
    if (normalized == QStringLiteral("combobox") || normalized == QStringLiteral("combo"))
        return ParamEditorType::ComboBox;
    return ParamEditorType::Text;
}

ParamDefinition parseParamDefinition(const YAML::Node &node)
{
    ParamDefinition param;
    if (!node || !node.IsMap())
        return param;

    param.key         = nodeString(firstNode(node, {"key", "name"}));
    param.label       = nodeString(firstNode(node, {"label", "name_cn"}), param.key);
    param.description = nodeString(node["description"]);
    param.editor_type = editorTypeFromString(nodeString(firstNode(node, {"editor_type", "control_type"}),
                                                        QStringLiteral("text")));

    const QVariant raw_value = nodeVariant(firstNode(node, {"value", "default_value"}));
    param.value             = raw_value;
    param.default_value     = nodeVariant(firstNode(node, {"default_value", "value"}));
    if (!param.default_value.isValid())
        param.default_value = raw_value;

    param.minimum_value = nodeVariant(node["minimum_value"]);
    param.maximum_value = nodeVariant(node["maximum_value"]);
    param.step_value    = nodeVariant(node["step_value"]);
    param.decimals      = node["decimals"] ? node["decimals"].as<int>() : 0;
    param.enabled       = node["enabled"] ? node["enabled"].as<bool>() : true;
    param.unit          = nodeString(node["unit"]);

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

    group.key         = nodeString(firstNode(node, {"key", "name"}));
    group.label       = nodeString(firstNode(node, {"label", "name_cn"}), group.key);
    group.description = nodeString(node["description"]);
    group.enabled     = node["enabled"] ? node["enabled"].as<bool>() : true;
    group.part_index  = node["part_index"] ? node["part_index"].as<int>() : 0;

    const YAML::Node params_node = node["params"];
    if (params_node && params_node.IsSequence())
    {
        for (const YAML::Node &param_node : params_node)
        {
            ParamDefinition param = parseParamDefinition(param_node);
            if (!param.key.isEmpty())
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
        if (!group.key.isEmpty())
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
        schema.model_name  = nodeString(firstNode(model_node, {"model_name", "type_name", "name"}),
                                       trimmed_type_name).trimmed();
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
