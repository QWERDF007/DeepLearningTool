#include "parameter/ParameterSchema.h"

#include "common/YamlUtils.h"
#include "parameter/DynamicOptions.h"

#include <spdlog/spdlog.h>

#include <QMetaType>

#include <algorithm>

namespace dltool::parameter {

namespace {

using dltool::common::yaml::nodeString;
using dltool::common::yaml::nodeVariant;

bool optionValueMatches(const QVariant &lhs, const QVariant &rhs)
{
    return lhs == rhs || lhs.toString() == rhs.toString();
}

QVariant mappedOptionValue(const ParameterSpec &parameter, const QVariant &value)
{
    if (value.userType() != QMetaType::QString)
        return value;

    const QVariant mapped = parameter.options_value_map.value(value.toString());
    return mapped.isValid() ? mapped : value;
}

bool hasValue(const QVariant &value)
{
    return value.isValid() && !value.toString().trimmed().isEmpty();
}

bool isAutomaticValue(const QVariant &value)
{
    return !hasValue(value) || value.toString().compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
}

QVariant typedScalar(const QString &type, const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return {};

    const QString normalized = type.trimmed().toLower();
    if (normalized == QStringLiteral("bool") || normalized == QStringLiteral("boolean"))
    {
        if (value.userType() == QMetaType::QString)
        {
            const QString text = value.toString().trimmed().toLower();
            return text == QStringLiteral("true") || text == QStringLiteral("1") || text == QStringLiteral("yes")
                || text == QStringLiteral("on");
        }
        return value.toBool();
    }
    if (normalized == QStringLiteral("int") || normalized == QStringLiteral("integer"))
        return value.toInt();
    if (normalized == QStringLiteral("double") || normalized == QStringLiteral("float")
        || normalized == QStringLiteral("real"))
        return value.toDouble();
    return value.toString();
}

QVariant firstOptionValue(const QVector<ParameterOption> &options)
{
    return options.isEmpty() ? QVariant{} : options.front().actual_value;
}

bool containsOptionValue(const QVector<ParameterOption> &options, const QVariant &value)
{
    return std::any_of(options.cbegin(), options.cend(), [&value](const ParameterOption &option)
                       { return optionValueMatches(option.actual_value, value); });
}

void appendCurrentValueOption(ParameterSpec &parameter, const QVariant &value,
                              const QVector<ParameterOption> &options)
{
    if (!hasValue(value) || isAutomaticValue(value) || containsOptionValue(options, value))
        return;

    const QString label = QString("当前值 (%1)").arg(value.toString());
    if (!parameter.options_value_map.contains(label))
    {
        parameter.options.append(label);
        parameter.options_value_map.insert(label, value);
    }
}

} // namespace

ParameterSpec parseParameterSpec(const YAML::Node &node)
{
    ParameterSpec parameter;
    if (!node || !node.IsMap())
        return parameter;

    parameter.name_en       = nodeString(node["name_en"]);
    parameter.name_cn       = nodeString(node["name_cn"], parameter.name_en);
    parameter.description   = nodeString(node["description"]);
    parameter.value         = nodeVariant(node["value"]);
    parameter.default_value = nodeVariant(node["default_value"]);
    if (!parameter.default_value.isValid())
        parameter.default_value = parameter.value;
    if (!parameter.value.isValid())
        parameter.value = parameter.default_value;

    parameter.value_type      = nodeString(node["value_type"], QStringLiteral("string"));
    parameter.value_range     = nodeVariant(node["value_range"]).toList();
    parameter.display_type    = nodeString(node["display_type"], QStringLiteral("text"));
    parameter.backend_key     = nodeString(node["backend_key"]);
    parameter.options_map     = nodeVariant(node["options_map"]).toMap();
    parameter.options_key_field = nodeString(node["options_key_field"]);
    const bool dynamic_type = nodeString(node["param_type"]).trimmed().compare(QStringLiteral("dynamic"),
                                                                                  Qt::CaseInsensitive)
                              == 0;
    parameter.kind = dynamic_type || !parameter.backend_key.trimmed().isEmpty() ? ParameterKind::Dynamic
                                                                                  : ParameterKind::Static;
    parameter.options_value_map = nodeVariant(node["options_values"]).toMap();
    parameter.enabled = node["enabled"] ? node["enabled"].as<bool>() : true;
    parameter.unit    = nodeString(node["unit"]);
    parameter.enabled_when = nodeString(node["enabled_when"]);
    parameter.model_param_name = nodeString(node["model_param_name"]);
    parameter.official_weight = node["official_weight"] ? node["official_weight"].as<bool>() : false;
    parameter.variant_param = nodeString(node["variant_param"]);

    const YAML::Node variants_node = node["variants"];
    if (variants_node && variants_node.IsSequence())
    {
        for (const YAML::Node &entry : variants_node)
            parameter.variants.append(nodeString(entry));
    }
    parameter.variant_name_template = nodeString(node["variant_name_template"]);

    const YAML::Node options_node = node["options"];
    if (options_node && options_node.IsSequence())
    {
        for (const YAML::Node &entry : options_node)
            parameter.options.append(nodeVariant(entry));
    }

    if (parameter.kind == ParameterKind::Dynamic)
        resolveParameterOptions(parameter);
    return parameter;
}

bool resolveParameterOptions(ParameterSpec &parameter, const QVariantMap &context)
{
    if (parameter.kind != ParameterKind::Dynamic && parameter.backend_key.trimmed().isEmpty())
        return true;

    parameter.kind        = ParameterKind::Dynamic;
    parameter.backend_key = parameter.backend_key.trimmed();
    if (parameter.backend_key.isEmpty())
    {
        spdlog::warn("动态参数缺少 backend_key: name_en={}", parameter.name_en.toUtf8().constData());
        return false;
    }

    parameter.display_type = parameter.display_type.trimmed().isEmpty() ? QStringLiteral("combo")
                                                                         : parameter.display_type.trimmed().toLower();

    const auto result = DynamicOptionsRegistry::instance().resolve(parameter.backend_key, context);
    if (!result.provider_found)
    {
        spdlog::warn("动态参数 provider 未注册: name_en={}, backend_key={}", parameter.name_en.toUtf8().constData(),
                     parameter.backend_key.toUtf8().constData());
        return false;
    }

    const QVariant configured_value = mappedOptionValue(
        parameter, parameter.value.isValid() ? parameter.value : parameter.default_value);
    parameter.options           = parameterOptionLabels(result.options);
    parameter.options_value_map = parameterOptionValueMap(result.options);

    if (isAutomaticValue(configured_value))
    {
        const QVariant selected_value = result.recommended_value.isValid() ? result.recommended_value
                                                                            : firstOptionValue(result.options);
        if (selected_value.isValid())
        {
            parameter.value         = selected_value;
            parameter.default_value = selected_value;
        }
    }
    else
    {
        parameter.value = configured_value;
        appendCurrentValueOption(parameter, configured_value, result.options);
        if (!parameter.default_value.isValid())
            parameter.default_value = configured_value;
    }

    return true;
}

QVariant normalizeParameterValue(const ParameterSpec &parameter, const QVariant &value, const QVariantMap &context)
{
    const QVariant typed = typedScalar(parameter.value_type, mappedOptionValue(parameter, value));
    if (parameter.kind != ParameterKind::Dynamic || parameter.backend_key.trimmed().isEmpty())
        return typed;

    if (!isAutomaticValue(typed))
        return typed;

    const auto result = DynamicOptionsRegistry::instance().resolve(parameter.backend_key, context);
    if (result.provider_found)
    {
        if (result.recommended_value.isValid())
            return result.recommended_value;

        const QVariant selected = firstOptionValue(result.options);
        if (selected.isValid())
            return selected;
    }

    const QVector<ParameterOption> options = parameterOptions(parameter.options, parameter.options_value_map);
    const QVariant selected = firstOptionValue(options);
    if (selected.isValid())
        return selected;
    return typed;
}

QVariantList parameterOptionLabels(const QVector<ParameterOption> &options)
{
    QVariantList labels;
    for (const ParameterOption &option : options)
    {
        if (!option.display_value.isEmpty() && !labels.contains(option.display_value))
            labels.append(option.display_value);
    }
    return labels;
}

QVariantMap parameterOptionValueMap(const QVector<ParameterOption> &options)
{
    QVariantMap values;
    for (const ParameterOption &option : options)
    {
        if (!option.display_value.isEmpty() && !values.contains(option.display_value))
            values.insert(option.display_value, option.actual_value);
    }
    return values;
}

QVector<ParameterOption> parameterOptions(const QVariantList &labels, const QVariantMap &value_map)
{
    QVector<ParameterOption> options;
    options.reserve(labels.size());
    for (const QVariant &raw_label : labels)
    {
        const QString label = raw_label.toString();
        options.push_back({label, value_map.value(label, raw_label)});
    }
    return options;
}

} // namespace dltool::parameter
