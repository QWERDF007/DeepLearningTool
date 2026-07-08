#include "model/ModelParamsSchema.h"

#include "common/Utils.h"
#include "common/YamlUtils.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <utility>

namespace dltool::model {

namespace {

using dltool::common::yaml::loadFile;
using dltool::common::yaml::nodeString;
using dltool::common::yaml::nodeVariant;

ParamDefinition parseParamDefinition(const YAML::Node &node)
{
    ParamDefinition param;
    if (!node || !node.IsMap())
        return param;

    param.name_en       = nodeString(node["name_en"]);
    param.name_cn       = nodeString(node["name_cn"], param.name_en);
    param.description   = nodeString(node["description"]);
    param.value         = nodeVariant(node["value"]);
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
        for (const YAML::Node &entry : options_node) param.options.append(nodeString(entry));
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

QFileInfo findModelConfigFile(const QString &framework_name, const QString &model_architecture)
{
    const QString config_root = dltool::common::runtimePath(QStringLiteral("config/models"));
    return dltool::common::yaml::findConfigFile(QDir(config_root).filePath(framework_name), model_architecture);
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

ModelParamsSchema loadModelParamsSchema(const QString &framework_name, const QString &model_architecture)
{
    const QString     trimmed_framework_name     = framework_name.trimmed();
    const QString     trimmed_model_architecture = model_architecture.trimmed();
    ModelParamsSchema schema;
    schema.framework_name     = trimmed_framework_name;
    schema.model_architecture = trimmed_model_architecture;
    schema.model_name         = trimmed_model_architecture;

    if (trimmed_framework_name.isEmpty() || trimmed_model_architecture.isEmpty())
        return schema;

    const QFileInfo config_file = findModelConfigFile(trimmed_framework_name, trimmed_model_architecture);
    if (!config_file.exists())
    {
        spdlog::warn("未找到 {}/{} 的模型配置文件", trimmed_framework_name.toUtf8().constData(),
                     trimmed_model_architecture.toUtf8().constData());
        return schema;
    }

    spdlog::info("加载模型参数配置: {}", config_file.absoluteFilePath().toUtf8().constData());

    try
    {
        const YAML::Node root = loadFile(config_file);
        if (!root.IsMap())
        {
            spdlog::warn("模型配置不是 map 类型: {}", config_file.absoluteFilePath().toUtf8().constData());
            return schema;
        }

        const YAML::Node model_node
            = looksLikeModelNode(root) ? root : root[dltool::common::yaml::toYamlString(trimmed_model_architecture)];
        if (!model_node || !model_node.IsMap())
        {
            spdlog::warn("模型配置中缺少模型 '{}': {}", trimmed_model_architecture.toUtf8().constData(),
                         config_file.absoluteFilePath().toUtf8().constData());
            return schema;
        }

        schema.config_path        = config_file.absoluteFilePath();
        schema.framework_name     = nodeString(model_node["framework"], trimmed_framework_name).trimmed();
        schema.model_architecture = nodeString(model_node["model_architecture"], trimmed_model_architecture).trimmed();
        schema.model_name         = nodeString(model_node["model_name"], trimmed_model_architecture).trimmed();
        if (schema.framework_name.isEmpty())
            schema.framework_name = trimmed_framework_name;
        if (schema.model_architecture.isEmpty())
            schema.model_architecture = trimmed_model_architecture;
        if (schema.model_name.isEmpty())
            schema.model_name = trimmed_model_architecture;
        schema.method = nodeString(model_node["method"]);

        parseGroups(model_node["train_params"], schema.train_groups);
        parseGroups(model_node["test_params"], schema.test_groups);
    }
    catch (const std::exception &e)
    {
        spdlog::error("解析模型配置失败 '{}': {}", config_file.absoluteFilePath().toUtf8().constData(), e.what());
        schema.train_groups.clear();
        schema.test_groups.clear();
    }

    return schema;
}

} // namespace dltool::model
