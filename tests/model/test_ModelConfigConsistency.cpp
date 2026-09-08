#include "../test_runner.h"

#include "common/Utils.h"
#include "common/YamlUtils.h"
#include "core/CoreDef.h"
#include "database/ModelDataBase.h"
#include "database/ModelTaskDataBase.h"
#include "model/ModelRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <yaml-cpp/yaml.h>

#include <cmath>
#include <cstdio>

namespace {

bool looksLikeModelNode(const YAML::Node &node)
{
    return node && node.IsMap() && (node["method"] || node["train_params"] || node["test_params"]);
}

QList<QPair<QString, YAML::Node>> modelNodes(const YAML::Node &root, const QString &file_path)
{
    QList<QPair<QString, YAML::Node>> result;
    if (looksLikeModelNode(root))
    {
        result.append({file_path, root});
        return result;
    }

    if (!root || !root.IsMap())
        return result;
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        const YAML::Node node = it->second;
        if (looksLikeModelNode(node))
            result.append({file_path + QStringLiteral("#") + QString::fromStdString(it->first.as<std::string>()), node});
    }
    return result;
}

void addError(QStringList &errors, const QString &file_path, const QString &message)
{
    errors.append(QStringLiteral("%1: %2").arg(file_path, message));
}

void reportErrors(const QStringList &errors)
{
    if (!errors.isEmpty())
        std::fprintf(stderr, "%s\n", errors.join(QChar('\n')).toUtf8().constData());
}

QString scalarString(const YAML::Node &node)
{
    if (!node || !node.IsScalar())
        return {};
    try
    {
        return QString::fromStdString(node.as<std::string>());
    }
    catch (const YAML::Exception &)
    {
        return {};
    }
}

bool hasChineseText(const QString &text)
{
    for (const QChar character : text)
    {
        const ushort code = character.unicode();
        if ((code >= 0x3400 && code <= 0x4DBF) || (code >= 0x4E00 && code <= 0x9FFF))
            return true;
    }
    return false;
}

bool isParameterName(const QString &name)
{
    if (name.isEmpty())
        return false;

    const auto isAsciiLetter = [](const QChar character)
    {
        return (character >= QChar('a') && character <= QChar('z'))
            || (character >= QChar('A') && character <= QChar('Z'));
    };
    const auto isNameCharacter = [&isAsciiLetter](const QChar character)
    {
        return isAsciiLetter(character) || (character >= QChar('0') && character <= QChar('9'))
            || character == QChar('_');
    };

    if (!isAsciiLetter(name.front()))
        return false;
    for (const QChar character : name.sliced(1))
    {
        if (!isNameCharacter(character))
            return false;
    }
    return true;
}

QString normalizedType(const QString &type)
{
    return type.trimmed().toLower();
}

bool isIntegerType(const QString &type)
{
    const QString normalized = normalizedType(type);
    return normalized == QStringLiteral("int") || normalized == QStringLiteral("integer");
}

bool isDoubleType(const QString &type)
{
    const QString normalized = normalizedType(type);
    return normalized == QStringLiteral("double") || normalized == QStringLiteral("float")
        || normalized == QStringLiteral("real");
}

bool isBooleanType(const QString &type)
{
    const QString normalized = normalizedType(type);
    return normalized == QStringLiteral("bool") || normalized == QStringLiteral("boolean");
}

bool isStringType(const QString &type)
{
    return normalizedType(type) == QStringLiteral("string");
}

bool isNumericType(const QString &type)
{
    return isIntegerType(type) || isDoubleType(type);
}

bool scalarMatchesType(const YAML::Node &node, const QString &type)
{
    if (!node || !node.IsScalar())
        return false;
    try
    {
        if (isIntegerType(type))
        {
            static_cast<void>(node.as<int>());
            return true;
        }
        if (isDoubleType(type))
        {
            const double value = node.as<double>();
            return std::isfinite(value);
        }
        if (isBooleanType(type))
        {
            static_cast<void>(node.as<bool>());
            return true;
        }
        if (isStringType(type))
        {
            static_cast<void>(node.as<std::string>());
            return true;
        }
    }
    catch (const YAML::Exception &)
    {
        return false;
    }
    return false;
}

bool scalarValuesEqual(const YAML::Node &lhs, const YAML::Node &rhs, const QString &type)
{
    if (!scalarMatchesType(lhs, type) || !scalarMatchesType(rhs, type))
        return false;
    try
    {
        if (isIntegerType(type))
            return lhs.as<int>() == rhs.as<int>();
        if (isDoubleType(type))
            return lhs.as<double>() == rhs.as<double>();
        if (isBooleanType(type))
            return lhs.as<bool>() == rhs.as<bool>();
        return lhs.as<std::string>() == rhs.as<std::string>();
    }
    catch (const YAML::Exception &)
    {
        return false;
    }
}

void validateParameterMetadata(QStringList &errors, const YAML::Node &param, const QString &location)
{
    if (!param || !param.IsMap())
    {
        addError(errors, location, QStringLiteral("参数定义必须是 map"));
        return;
    }

    const YAML::Node name_node = param["name_en"];
    const YAML::Node name_cn_node = param["name_cn"];
    const YAML::Node description_node = param["description"];
    const YAML::Node type_node = param["value_type"];
    const YAML::Node display_node = param["display_type"];
    const QString    name = scalarString(name_node).trimmed();
    const QString    name_cn = scalarString(name_cn_node).trimmed();
    const QString    description = scalarString(description_node).trimmed();
    const QString    type = scalarString(type_node).trimmed();
    const QString    display = scalarString(display_node).trimmed().toLower();

    if (!name_node || !name_node.IsScalar() || !isParameterName(name))
        addError(errors, location, QStringLiteral("name_en 必须是 ASCII 参数名"));
    if (!name_cn_node || !name_cn_node.IsScalar() || name_cn.isEmpty())
        addError(errors, location, QStringLiteral("name_cn 不能为空"));
    if (!description_node || !description_node.IsScalar() || description.isEmpty() || !hasChineseText(description))
        addError(errors, location, QStringLiteral("description 必须包含中文说明"));
    if (!type_node || !type_node.IsScalar()
        || !(isIntegerType(type) || isDoubleType(type) || isBooleanType(type) || isStringType(type)))
    {
        addError(errors, location, QStringLiteral("value_type 必须是 int、double、bool 或 string"));
        return;
    }
    if (!display_node || !display_node.IsScalar())
    {
        addError(errors, location, QStringLiteral("display_type 不能为空"));
    }
    else if (!QStringList{QStringLiteral("spin"), QStringLiteral("slider"), QStringLiteral("checkbox"),
                          QStringLiteral("combo"), QStringLiteral("group_combo"), QStringLiteral("text"),
                          QStringLiteral("file"), QStringLiteral("path")}
                  .contains(display))
    {
        addError(errors, location, QStringLiteral("display_type 不受支持: %1").arg(display));
    }

    const bool has_backend_key = param["backend_key"] && !scalarString(param["backend_key"]).trimmed().isEmpty();
    const QString param_type = scalarString(param["param_type"]).trimmed().toLower();
    const bool declared_dynamic = param_type == QStringLiteral("dynamic");
    if (param["param_type"] && !declared_dynamic)
        addError(errors, location, QStringLiteral("param_type 只能声明为 dynamic"));
    if (declared_dynamic && !has_backend_key)
        addError(errors, location, QStringLiteral("dynamic 参数必须提供 backend_key"));
    if (display == QStringLiteral("group_combo") && !has_backend_key)
        addError(errors, location, QStringLiteral("group_combo 参数必须是 dynamic 参数"));

    const bool dynamic = declared_dynamic || has_backend_key;
    const YAML::Node value_node = param["value"];
    const YAML::Node default_node = param["default_value"];
    if (!value_node && !default_node && !dynamic)
        addError(errors, location, QStringLiteral("静态参数必须提供 value 或 default_value"));
    if (value_node && !scalarMatchesType(value_node, type))
        addError(errors, location, QStringLiteral("value 与 value_type 不匹配"));
    if (default_node && !scalarMatchesType(default_node, type))
        addError(errors, location, QStringLiteral("default_value 与 value_type 不匹配"));

    if (isNumericType(type))
    {
        const YAML::Node range = param["value_range"];
        const YAML::Node options = param["options"];
        if (!range && (!options || !options.IsSequence() || options.size() == 0))
        {
            addError(errors, location, QStringLiteral("数值参数必须提供 value_range 或 options"));
        }
        else if (range && (!range.IsSequence() || range.size() != 3))
        {
            addError(errors, location, QStringLiteral("value_range 必须是 [最小值, 最大值, 步长]"));
        }
        else if (range)
        {
            try
            {
                const double minimum = range[0].as<double>();
                const double maximum = range[1].as<double>();
                const double step = range[2].as<double>();
                if (!std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(step) || minimum > maximum
                    || step <= 0.0)
                {
                    addError(errors, location, QStringLiteral("value_range 必须满足有限、最小值不大于最大值、步长为正"));
                }
                if (value_node && scalarMatchesType(value_node, type))
                {
                    const double value = value_node.as<double>();
                    if (value < minimum || value > maximum)
                        addError(errors, location, QStringLiteral("value 必须位于 value_range 内"));
                }
            }
            catch (const YAML::Exception &)
            {
                addError(errors, location, QStringLiteral("value_range 必须是数值序列"));
            }
        }
    }
    else if (param["value_range"])
    {
        addError(errors, location, QStringLiteral("非数值参数不应提供 value_range"));
    }

    const YAML::Node options = param["options"];
    if (options)
    {
        if (!options.IsSequence() || options.size() == 0)
        {
            addError(errors, location, QStringLiteral("options 必须是非空序列"));
        }
        else
        {
            bool current_value_found = !value_node && !default_node;
            QSet<QString> option_values;
            for (const YAML::Node &option : options)
            {
                if (!scalarMatchesType(option, type))
                    addError(errors, location, QStringLiteral("options 中存在与 value_type 不匹配的值"));
                const QString option_text = scalarString(option).trimmed();
                if (option_text.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0)
                    addError(errors, location, QStringLiteral("options 不得包含 auto"));
                if (option_values.contains(option_text))
                    addError(errors, location, QStringLiteral("options 不得包含重复值"));
                option_values.insert(option_text);
                if (value_node && scalarValuesEqual(option, value_node, type))
                    current_value_found = true;
                if (!value_node && default_node && scalarValuesEqual(option, default_node, type))
                    current_value_found = true;
            }
            if (display == QStringLiteral("combo") && !dynamic && !current_value_found)
                addError(errors, location, QStringLiteral("静态 combo 的 value 必须出现在 options 中"));
        }
    }
    else if (display == QStringLiteral("combo") && !dynamic)
    {
        addError(errors, location, QStringLiteral("静态 combo 必须提供 options"));
    }

    for (const YAML::Node &value_key : {value_node, default_node})
    {
        const QString text = scalarString(value_key).trimmed();
        if (text.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0)
            addError(errors, location, QStringLiteral("参数值不得使用 auto"));
    }
}

void validateModelMetadata(QStringList &errors, const YAML::Node &model, const QString &location)
{
    if (!model || !model.IsMap())
    {
        addError(errors, location, QStringLiteral("模型节点必须是 map"));
        return;
    }

    for (const char *key : {"framework", "model_architecture", "model_name", "method"})
    {
        const YAML::Node value = model[key];
        if (!value || !value.IsScalar() || scalarString(value).trimmed().isEmpty())
            addError(errors, location, QStringLiteral("模型字段 %1 不能为空").arg(QString::fromLatin1(key)));
    }

    for (const char *section_name : {"train_params", "test_params"})
    {
        const YAML::Node groups = model[section_name];
        if (!groups || !groups.IsSequence())
        {
            addError(errors, location, QStringLiteral("%1 必须是参数组序列").arg(QString::fromLatin1(section_name)));
            continue;
        }

        QSet<QString> group_names;
        for (const YAML::Node &group : groups)
        {
            const QString group_location = location + QStringLiteral(".") + QString::fromLatin1(section_name);
            if (!group || !group.IsMap())
            {
                addError(errors, group_location, QStringLiteral("参数组必须是 map"));
                continue;
            }
            const QString group_name = scalarString(group["name_en"]).trimmed();
            if (group_name.isEmpty() || !group["name_en"].IsScalar())
                addError(errors, group_location, QStringLiteral("参数组 name_en 不能为空"));
            if (!group["name_cn"] || !group["name_cn"].IsScalar()
                || scalarString(group["name_cn"]).trimmed().isEmpty())
                addError(errors, group_location, QStringLiteral("参数组 name_cn 不能为空"));
            if (!group["part_index"] || !group["part_index"].IsScalar())
                addError(errors, group_location, QStringLiteral("参数组必须提供 part_index"));
            else
            {
                try
                {
                    if (group["part_index"].as<int>() < 0)
                        addError(errors, group_location, QStringLiteral("part_index 不能为负数"));
                }
                catch (const YAML::Exception &)
                {
                    addError(errors, group_location, QStringLiteral("part_index 必须是整数"));
                }
            }
            if (group["enabled"] && !group["enabled"].IsScalar())
                addError(errors, group_location, QStringLiteral("参数组 enabled 必须是 bool"));
            if (group_names.contains(group_name))
                addError(errors, group_location, QStringLiteral("参数组 name_en 重复: %1").arg(group_name));
            group_names.insert(group_name);

            const YAML::Node params = group["params"];
            if (!params || !params.IsSequence())
            {
                addError(errors, group_location, QStringLiteral("params 必须是参数序列"));
                continue;
            }
            QSet<QString> parameter_names;
            for (const YAML::Node &param : params)
            {
                const QString parameter_name = scalarString(param["name_en"]).trimmed();
                const QString parameter_location
                    = group_location + QStringLiteral(".") + group_name + QStringLiteral(".") + parameter_name;
                validateParameterMetadata(errors, param, parameter_location);
                if (parameter_names.contains(parameter_name))
                    addError(errors, parameter_location, QStringLiteral("参数 name_en 重复: %1").arg(parameter_name));
                parameter_names.insert(parameter_name);
            }
        }
    }
}

bool hasGroup(const YAML::Node &groups, const QString &name)
{
    if (!groups || !groups.IsSequence())
        return false;
    for (const YAML::Node &group : groups)
    {
        if (group && group.IsMap() && group["name_en"]
            && QString::fromStdString(group["name_en"].as<std::string>()) == name)
            return true;
    }
    return false;
}

bool hasParameter(const YAML::Node &group, const QString &name)
{
    const YAML::Node params = group["params"];
    if (!params || !params.IsSequence())
        return false;
    for (const YAML::Node &param : params)
    {
        if (param && param.IsMap() && param["name_en"]
            && QString::fromStdString(param["name_en"].as<std::string>()) == name)
            return true;
    }
    return false;
}

YAML::Node findParameter(const YAML::Node &group, const QString &name)
{
    const YAML::Node params = group["params"];
    if (!params || !params.IsSequence())
        return {};
    for (const YAML::Node &param : params)
    {
        if (param && param.IsMap() && param["name_en"]
            && QString::fromStdString(param["name_en"].as<std::string>()) == name)
            return param;
    }
    return {};
}

const YAML::Node findGroup(const YAML::Node &groups, const QString &name)
{
    if (!groups || !groups.IsSequence())
        return {};
    for (const YAML::Node &group : groups)
    {
        if (group && group.IsMap() && group["name_en"]
            && QString::fromStdString(group["name_en"].as<std::string>()) == name)
            return group;
    }
    return {};
}

} // namespace

class ModelConfigConsistencyTest : public QObject
{
    Q_OBJECT

private slots:
    void runtimeModelConfigsHaveCompleteParameterMetadata()
    {
        const QDir models_root(dltool::common::runtimePath(QStringLiteral("config/models")));
        QVERIFY2(models_root.exists(), qPrintable(QStringLiteral("模型配置目录不存在: %1").arg(models_root.absolutePath())));

        QStringList errors;
        int         checked_models = 0;
        const QFileInfoList framework_dirs
            = models_root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &framework_dir : framework_dirs)
        {
            const QFileInfoList files
                = QDir(framework_dir.absoluteFilePath())
                      .entryInfoList({QStringLiteral("*.yaml"), QStringLiteral("*.yml")}, QDir::Files, QDir::Name);
            for (const QFileInfo &file : files)
            {
                try
                {
                    const YAML::Node root = dltool::common::yaml::loadFile(file);
                    const auto         nodes = modelNodes(root, file.absoluteFilePath());
                    if (nodes.isEmpty())
                    {
                        addError(errors, file.absoluteFilePath(), QStringLiteral("未找到模型节点"));
                        continue;
                    }
                    for (const auto &entry : nodes)
                    {
                        ++checked_models;
                        validateModelMetadata(errors, entry.second, entry.first);
                    }
                }
                catch (const std::exception &error)
                {
                    addError(errors, file.absoluteFilePath(), QString::fromUtf8(error.what()));
                }
            }
        }

        QVERIFY2(checked_models > 0, "没有扫描到模型配置");
        reportErrors(errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QChar('\n'))));
    }

    void runtimeModelConfigsUseConservativeDataLoaderWorkerSettings()
    {
        const QDir models_root(dltool::common::runtimePath(QStringLiteral("config/models")));
        QVERIFY2(models_root.exists(), qPrintable(QStringLiteral("模型配置目录不存在: %1").arg(models_root.absolutePath())));

        QStringList errors;
        int         checked_workers = 0;
        const QFileInfoList framework_dirs
            = models_root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &framework_dir : framework_dirs)
        {
            const QFileInfoList files
                = QDir(framework_dir.absoluteFilePath())
                      .entryInfoList({QStringLiteral("*.yaml"), QStringLiteral("*.yml")}, QDir::Files, QDir::Name);
            for (const QFileInfo &file : files)
            {
                try
                {
                    const YAML::Node root = dltool::common::yaml::loadFile(file);
                    for (const auto &entry : modelNodes(root, file.absoluteFilePath()))
                    {
                        const YAML::Node &model = entry.second;
                        for (const QString &section_name : {QStringLiteral("train_params"), QStringLiteral("test_params")})
                        {
                            const YAML::Node groups = model[section_name.toStdString()];
                            if (!groups || !groups.IsSequence())
                                continue;
                            for (const YAML::Node &group : groups)
                            {
                                const YAML::Node params = group["params"];
                                if (!params || !params.IsSequence())
                                    continue;
                                for (const YAML::Node &param : params)
                                {
                                    if (!param || !param["name_en"])
                                        continue;
                                    const QString name = QString::fromStdString(param["name_en"].as<std::string>());
                                    if (name != QStringLiteral("workers") && name != QStringLiteral("num_workers")
                                        && name != QStringLiteral("nworker"))
                                    {
                                        continue;
                                    }

                                    ++checked_workers;
                                    const QString location = QStringLiteral("%1:%2.%3.%4")
                                                                  .arg(entry.first, section_name,
                                                                       group["name_en"]
                                                                           ? QString::fromStdString(
                                                                                 group["name_en"].as<std::string>())
                                                                           : QStringLiteral("<group>"),
                                                                       name);
                                    if (!param["value_type"]
                                        || QString::fromStdString(param["value_type"].as<std::string>())
                                               != QStringLiteral("int"))
                                    {
                                        addError(errors, location, QStringLiteral("数据加载进程参数必须是 int"));
                                    }
                                    if (!param["value"] || param["value"].as<int>() != 2)
                                        addError(errors, location, QStringLiteral("默认值必须为 2"));

                                    const YAML::Node range = param["value_range"];
                                    if (!range || !range.IsSequence() || range.size() != 3 || range[0].as<int>() != 0
                                        || range[1].as<int>() != 128 || range[2].as<int>() != 1)
                                    {
                                        addError(errors, location, QStringLiteral("取值范围必须为 [0, 128, 1]"));
                                    }
                                }
                            }
                        }
                    }
                }
                catch (const std::exception &error)
                {
                    addError(errors, file.absoluteFilePath(), QString::fromUtf8(error.what()));
                }
            }
        }

        QVERIFY2(checked_workers > 0, "没有扫描到数据加载进程参数");
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QChar('\n'))));
    }

    void runtimeModelConfigsExposeSourceDefaultsAndSemanticNames()
    {
        const QDir models_root(dltool::common::runtimePath(QStringLiteral("config/models")));
        QVERIFY2(models_root.exists(), qPrintable(QStringLiteral("模型配置目录不存在: %1").arg(models_root.absolutePath())));

        QStringList errors;
        int         checked_parameters = 0;
        const auto checkName = [&errors, &checked_parameters](const YAML::Node &group, const QString &name,
                                                               const QString &expected_name_cn)
        {
            const YAML::Node param = findParameter(group, name);
            if (!param)
            {
                errors.append(QStringLiteral("缺少参数 %1").arg(name));
                return YAML::Node{};
            }
            ++checked_parameters;
            const QString actual_name_cn
                = param["name_cn"] ? QString::fromStdString(param["name_cn"].as<std::string>()) : QString();
            if (actual_name_cn != expected_name_cn)
                errors.append(QStringLiteral("参数 %1 的 name_cn 应为 %2，实际为 %3")
                                  .arg(name, expected_name_cn, actual_name_cn));
            if (!param["description"] || QString::fromStdString(param["description"].as<std::string>()).trimmed().isEmpty())
                errors.append(QStringLiteral("参数 %1 缺少中文简介").arg(name));
            return param;
        };
        const auto checkIntValue = [&errors](const YAML::Node &param, const QString &name, const int expected)
        {
            if (param && (!param["value"] || param["value"].as<int>() != expected))
                errors.append(QStringLiteral("参数 %1 的默认整数值应为 %2").arg(name).arg(expected));
        };
        const auto checkDoubleValue = [&errors](const YAML::Node &param, const QString &name, const double expected)
        {
            if (param && (!param["value"] || std::abs(param["value"].as<double>() - expected) > 1e-12))
                errors.append(QStringLiteral("参数 %1 的默认浮点值应为 %2").arg(name).arg(expected, 0, 'g', 15));
        };
        const auto checkDescription = [&errors](const YAML::Node &param, const QString &name,
                                                 const QString &expected)
        {
            if (param && (!param["description"]
                          || QString::fromStdString(param["description"].as<std::string>()) != expected))
            {
                errors.append(QStringLiteral("参数 %1 的简介不准确，应为：%2").arg(name, expected));
            }
        };

        const QFileInfoList framework_dirs
            = models_root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &framework_dir : framework_dirs)
        {
            const QFileInfoList files
                = QDir(framework_dir.absoluteFilePath())
                      .entryInfoList({QStringLiteral("*.yaml"), QStringLiteral("*.yml")}, QDir::Files, QDir::Name);
            for (const QFileInfo &file : files)
            {
                try
                {
                    const YAML::Node root = dltool::common::yaml::loadFile(file);
                    for (const auto &entry : modelNodes(root, file.absoluteFilePath()))
                    {
                        const YAML::Node &model      = entry.second;
                        const QString     framework  = model["framework"]
                                                          ? QString::fromStdString(model["framework"].as<std::string>())
                                                          : framework_dir.fileName();
                        const QString     architecture = model["model_architecture"]
                                                            ? QString::fromStdString(
                                                                  model["model_architecture"].as<std::string>())
                                                            : QString();
                        const YAML::Node train_params = model["train_params"];
                        const YAML::Node training     = findGroup(train_params, QStringLiteral("training"));
                        const YAML::Node network      = findGroup(train_params, QStringLiteral("network"));
                        const YAML::Node augmentation = findGroup(train_params, QStringLiteral("augmentation"));

                        if (framework.compare(QStringLiteral("ultralytics"), Qt::CaseInsensitive) == 0)
                        {
                            checkName(network, QStringLiteral("model"), QStringLiteral("模型规格"));
                            checkName(network, QStringLiteral("checkpoint"), QStringLiteral("初始权重"));
                            checkName(network, QStringLiteral("imgsz"), QStringLiteral("输入图像尺寸"));
                            const YAML::Node batch = checkName(training, QStringLiteral("batch"), QStringLiteral("批量大小"));
                            checkIntValue(batch, QStringLiteral("batch"), 8);
                            const YAML::Node optimizer
                                = checkName(training, QStringLiteral("optimizer"), QStringLiteral("优化器"));
                            if (optimizer && (!optimizer["value"]
                                              || QString::fromStdString(optimizer["value"].as<std::string>())
                                                     != QStringLiteral("AdamW")))
                            {
                                errors.append(QStringLiteral("Ultralytics 默认优化器必须是具体值 AdamW"));
                            }
                            if (optimizer && optimizer["options"] && optimizer["options"].IsSequence())
                            {
                                for (const YAML::Node &option : optimizer["options"])
                                {
                                    if (option.IsScalar()
                                        && QString::fromStdString(option.as<std::string>()) == QStringLiteral("auto"))
                                        errors.append(QStringLiteral("Ultralytics 优化器选项不得包含 auto"));
                                }
                            }
                            const YAML::Node momentum
                                = checkName(training, QStringLiteral("momentum"), QStringLiteral("动量系数"));
                            checkDescription(
                                momentum, QStringLiteral("momentum"),
                                QStringLiteral("SGD、MuSGD 和 RMSprop 使用该值作为动量系数；Adam、AdamW、Adamax、NAdam、RAdam 使用该值作为 beta1（一阶矩衰减系数）。"));
                            if (momentum && momentum["enabled_when"])
                                errors.append(QStringLiteral("Ultralytics momentum 不应限制为特定优化器"));
                            const YAML::Node lr0
                                = checkName(training, QStringLiteral("lr0"), QStringLiteral("初始学习率"));
                            checkDoubleValue(lr0, QStringLiteral("lr0"), 0.001);
                            checkDescription(
                                lr0, QStringLiteral("lr0"),
                                QStringLiteral("AdamW 的初始学习率；Ultralytics 默认配置对 Adam/AdamW 的建议值为 0.001。"));
                            const YAML::Node hsv_h
                                = checkName(augmentation, QStringLiteral("hsv_h"), QStringLiteral("HSV 色相增强比例"));
                            checkDescription(
                                hsv_h, QStringLiteral("hsv_h"),
                                QStringLiteral("HSV 色相随机增强的最大比例；实际偏移量为 [-该值, 该值] × 180。"));
                            const YAML::Node hsv_s = checkName(
                                augmentation, QStringLiteral("hsv_s"), QStringLiteral("HSV 饱和度增强比例"));
                            checkDescription(
                                hsv_s, QStringLiteral("hsv_s"),
                                QStringLiteral("HSV 饱和度随机增强的最大比例；实际缩放因子为 1 + [-该值, 该值]，结果裁剪到 [0, 255]。"));
                            const YAML::Node hsv_v
                                = checkName(augmentation, QStringLiteral("hsv_v"), QStringLiteral("HSV 明度增强比例"));
                            checkDescription(
                                hsv_v, QStringLiteral("hsv_v"),
                                QStringLiteral("HSV 明度随机增强的最大比例；实际缩放因子为 1 + [-该值, 该值]，结果裁剪到 [0, 255]。"));
                            const YAML::Node val
                                = checkName(training, QStringLiteral("val"), QStringLiteral("验证间隔"));
                            if (val && (!val["value_type"]
                                         || QString::fromStdString(val["value_type"].as<std::string>())
                                                != QStringLiteral("int")))
                            {
                                errors.append(QStringLiteral("val 必须是 int，不能误改为布尔值"));
                            }
                            checkIntValue(val, QStringLiteral("val"), 1);
                            if (hasParameter(training, QStringLiteral("val_interval")))
                            {
                                errors.append(QStringLiteral("Ultralytics 训练参数中不得存在别名 val_interval，应统一使用 val"));
                            }
                        }
                        else if (framework.compare(QStringLiteral("anomalib"), Qt::CaseInsensitive) == 0
                                 && architecture.compare(QStringLiteral("patchcore"), Qt::CaseInsensitive) == 0)
                        {
                            const YAML::Node crop
                                = checkName(network, QStringLiteral("center_crop_size"), QStringLiteral("中心裁剪尺寸"));
                            checkIntValue(crop, QStringLiteral("center_crop_size"), 0);
                            checkDescription(
                                crop, QStringLiteral("center_crop_size"),
                                QStringLiteral("先将输入图像缩放到 image_size，再从中心裁剪到该边长；训练、验证/测试和预测使用相同预处理，设为 0 表示不裁剪。"));
                            checkName(network, QStringLiteral("pre_trained"), QStringLiteral("使用预训练权重"));
                            const YAML::Node checkpoint
                                = checkName(network, QStringLiteral("checkpoint"), QStringLiteral("初始检查点"));
                            checkDescription(
                                checkpoint, QStringLiteral("checkpoint"),
                                QStringLiteral("训练开始时恢复的完整训练检查点；留空则从当前网络配置创建模型后开始训练。"));
                            checkName(training, QStringLiteral("max_epochs"), QStringLiteral("训练轮数"));
                        }
                        else if (framework.compare(QStringLiteral("anomalib"), Qt::CaseInsensitive) == 0
                                 && architecture.compare(QStringLiteral("dinomaly2"), Qt::CaseInsensitive) == 0)
                        {
                            checkName(network, QStringLiteral("encoder_name"), QStringLiteral("编码器"));
                            checkName(network, QStringLiteral("crop_size"), QStringLiteral("中心裁剪尺寸"));
                            const YAML::Node crop = findParameter(network, QStringLiteral("crop_size"));
                            checkDescription(
                                crop, QStringLiteral("crop_size"),
                                QStringLiteral("先将输入图像缩放到 image_size，再从中心裁剪到该边长；训练、验证/测试和预测使用相同预处理。"));
                            const YAML::Node checkpoint
                                = checkName(network, QStringLiteral("checkpoint"), QStringLiteral("初始检查点"));
                            checkDescription(
                                checkpoint, QStringLiteral("checkpoint"),
                                QStringLiteral("训练开始时恢复的完整训练检查点；留空则从当前网络配置创建模型后开始训练。"));
                            const YAML::Node check_val
                                = checkName(training, QStringLiteral("check_val_every_n_epoch"), QStringLiteral("验证间隔"));
                            checkIntValue(check_val, QStringLiteral("check_val_every_n_epoch"), 1);
                            checkDescription(
                                check_val, QStringLiteral("check_val_every_n_epoch"),
                                QStringLiteral("每 N 个 Epoch 运行一次验证。"));
                            const YAML::Node checkpoint_interval = checkName(
                                training, QStringLiteral("checkpoint_every_n_epoch"), QStringLiteral("保存间隔"));
                            checkIntValue(checkpoint_interval, QStringLiteral("checkpoint_every_n_epoch"), 1);
                            checkDescription(
                                checkpoint_interval, QStringLiteral("checkpoint_every_n_epoch"),
                                QStringLiteral("每 N 个 Epoch 保存一次模型检查点。"));
                            const YAML::Node recentring = checkName(
                                training, QStringLiteral("use_context_recentering"), QStringLiteral("上下文特征重居中"));
                            if (recentring && (!recentring["value"] || recentring["value"].as<bool>()))
                                errors.append(QStringLiteral("Anomalib Dinomaly 默认不应启用上下文特征重居中"));
                            checkName(training, QStringLiteral("max_steps"), QStringLiteral("最大训练步数"));
                        }
                        else if (framework.compare(QStringLiteral("dinomaly2"), Qt::CaseInsensitive) == 0)
                        {
                            checkName(network, QStringLiteral("backbone"), QStringLiteral("编码器"));
                            const YAML::Node crop = checkName(
                                network, QStringLiteral("crop_size"), QStringLiteral("中心裁剪尺寸"));
                            checkDescription(
                                crop, QStringLiteral("crop_size"),
                                QStringLiteral("先将输入图像缩放到 image_size，再从中心裁剪到该边长；训练、验证/测试和预测使用相同预处理。"));
                            const YAML::Node checkpoint
                                = checkName(network, QStringLiteral("checkpoint"), QStringLiteral("初始检查点"));
                            checkDescription(
                                checkpoint, QStringLiteral("checkpoint"),
                                QStringLiteral("训练开始时加载的模型状态字典文件；留空则从当前网络配置初始化模型。"));
                            const YAML::Node linear_attention
                                = checkName(network, QStringLiteral("la"), QStringLiteral("线性注意力"));
                            if (linear_attention
                                && (!linear_attention["description"]
                                    || QString::fromStdString(linear_attention["description"].as<std::string>())
                                           != QStringLiteral("1 表示使用线性注意力，0 表示使用标准注意力。")))
                            {
                                errors.append(QStringLiteral("参数 la 的简介必须准确说明 true/false 对应的注意力实现"));
                            }
                            checkName(training, QStringLiteral("max_iters"), QStringLiteral("最大训练步数"));
                            checkName(training, QStringLiteral("lr_decay_ratio"), QStringLiteral("最终学习率比例"));
                        }
                        else if (framework.compare(QStringLiteral("FS-SAM2"), Qt::CaseInsensitive) == 0)
                        {
                            const YAML::Node fs_training = findGroup(train_params, QStringLiteral("training"));
                            const YAML::Node batch
                                = checkName(fs_training, QStringLiteral("batch_size"), QStringLiteral("批量大小"));
                            checkIntValue(batch, QStringLiteral("batch_size"), 8);
                            const YAML::Node workers
                                = checkName(fs_training, QStringLiteral("num_workers"), QStringLiteral("数据加载进程数"));
                            checkDescription(
                                workers, QStringLiteral("num_workers"),
                                QStringLiteral("用于 PyTorch DataLoader 并行读取和预处理数据的进程数。"));
                            checkName(fs_training, QStringLiteral("weight_decay"), QStringLiteral("权重衰减"));
                        }
                    }
                }
                catch (const std::exception &error)
                {
                    addError(errors, file.absoluteFilePath(), QString::fromUtf8(error.what()));
                }
            }
        }

        QVERIFY2(checked_parameters > 0, "没有检查模型参数语义");
        reportErrors(errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QChar('\n'))));
    }

    void runtimeModelConfigsUseStrictInferenceAndEvaluationGroups()
    {
        const QDir models_root(dltool::common::runtimePath(QStringLiteral("config/models")));
        QVERIFY2(models_root.exists(), qPrintable(QStringLiteral("模型配置目录不存在: %1").arg(models_root.absolutePath())));

        QStringList errors;
        int         checked_models = 0;
        const QFileInfoList framework_dirs
            = models_root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &framework_dir : framework_dirs)
        {
            if (framework_dir.fileName().compare(QStringLiteral("FS-SAM2"), Qt::CaseInsensitive) == 0)
                continue;

            const QFileInfoList files = QDir(framework_dir.absoluteFilePath())
                                            .entryInfoList({QStringLiteral("*.yaml"), QStringLiteral("*.yml")},
                                                           QDir::Files, QDir::Name);
            for (const QFileInfo &file : files)
            {
                try
                {
                    const YAML::Node root = dltool::common::yaml::loadFile(file);
                    const auto         nodes = modelNodes(root, file.absoluteFilePath());
                    if (nodes.isEmpty())
                    {
                        addError(errors, file.absoluteFilePath(), QStringLiteral("未找到模型节点"));
                        continue;
                    }

                    for (const auto &entry : nodes)
                    {
                        ++checked_models;
                        const QString     model_path = entry.first;
                        const YAML::Node &model      = entry.second;
                        const QString     method
                            = model["method"] ? QString::fromStdString(model["method"].as<std::string>()) : QString();
                        const YAML::Node groups = model["test_params"];
                        if (!groups || !groups.IsSequence() || groups.size() != 2)
                        {
                            addError(errors, model_path,
                                     QStringLiteral("test_params 必须严格包含 inference 和 evaluation 两组"));
                            continue;
                        }

                        const YAML::Node inference = groups[0];
                        const YAML::Node evaluation = groups[1];
                        const auto checkGroup = [&errors, &model_path](const YAML::Node &group, const QString &name,
                                                                       const QString &name_cn, const int part_index)
                        {
                            if (!group || !group.IsMap())
                            {
                                addError(errors, model_path, QStringLiteral("参数组 %1 不是 map").arg(name));
                                return;
                            }
                            const QString actual_name
                                = group["name_en"] ? QString::fromStdString(group["name_en"].as<std::string>())
                                                    : QString();
                            const QString actual_name_cn
                                = group["name_cn"] ? QString::fromStdString(group["name_cn"].as<std::string>())
                                                     : QString();
                            if (actual_name != name)
                                addError(errors, model_path, QStringLiteral("第一个/第二个参数组 name_en 应为 %1，实际为 %2")
                                                       .arg(name, actual_name));
                            if (actual_name_cn != name_cn)
                                addError(errors, model_path, QStringLiteral("参数组 %1 的 name_cn 应为 %2，实际为 %3")
                                                       .arg(name, name_cn, actual_name_cn));
                            if (!group["part_index"] || !group["part_index"].IsScalar()
                                || group["part_index"].as<int>() != part_index)
                                addError(errors, model_path,
                                         QStringLiteral("参数组 %1 的 part_index 应为 %2").arg(name).arg(part_index));
                        };
                        checkGroup(inference, QStringLiteral("inference"), QStringLiteral("推理参数"), 0);
                        checkGroup(evaluation, QStringLiteral("evaluation"), QStringLiteral("评估参数"), 1);

                        if (method.compare(QStringLiteral("AnomalyDetection"), Qt::CaseInsensitive) == 0)
                        {
                            if (!hasParameter(evaluation, QStringLiteral("classification_threshold")))
                                addError(errors, model_path, QStringLiteral("异常检测评估组缺少 classification_threshold"));
                        }
                        else if (method.compare(QStringLiteral("Detection"), Qt::CaseInsensitive) == 0
                                 || method.compare(QStringLiteral("Segmentation"), Qt::CaseInsensitive) == 0)
                        {
                            for (const QString &name : {QStringLiteral("conf"), QStringLiteral("iou"),
                                                          QStringLiteral("matching_strategy")})
                            {
                                if (!hasParameter(evaluation, name))
                                    addError(errors, model_path, QStringLiteral("检测/分割评估组缺少 %1").arg(name));
                            }

                            const YAML::Node matching = [&evaluation]() -> YAML::Node
                            {
                                const YAML::Node params = evaluation["params"];
                                if (!params || !params.IsSequence())
                                    return {};
                                for (const YAML::Node &param : params)
                                {
                                    if (param["name_en"]
                                        && param["name_en"].as<std::string>() == "matching_strategy")
                                        return param;
                                }
                                return {};
                            }();
                            const YAML::Node options = matching["options"];
                            if (!matching || matching["display_type"].as<std::string>() != "combo" || !options
                                || !options.IsSequence() || options.size() != 2
                                || options[0].as<std::string>() != "greedy_iou"
                                || options[1].as<std::string>() != "hungarian_iou")
                            {
                                addError(errors, model_path,
                                         QStringLiteral("matching_strategy 必须是 combo，选项必须为 greedy_iou、hungarian_iou"));
                            }
                        }
                    }
                }
                catch (const std::exception &error)
                {
                    addError(errors, file.absoluteFilePath(), QString::fromUtf8(error.what()));
                }
            }
        }

        QVERIFY2(checked_models > 0, "没有扫描到普通模型配置");
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QChar('\n'))));
    }

    void ultralyticsParametersMatchExecutionAndPersistenceContracts()
    {
        // 1. 验证 Ultralytics 各模型 (YOLOv8, YOLOv5, YOLOv8-seg) 拥有统一正确的参数契约
        const QDir models_root(dltool::common::runtimePath(QStringLiteral("config/models/ultralytics")));
        QVERIFY2(models_root.exists(), qPrintable(QStringLiteral("Ultralytics 目录不存在: %1").arg(models_root.absolutePath())));

        const QFileInfoList files = models_root.entryInfoList({QStringLiteral("*.yaml"), QStringLiteral("*.yml")},
                                                              QDir::Files, QDir::Name);
        QVERIFY(!files.isEmpty());

        for (const QFileInfo &file : files)
        {
            const YAML::Node root = dltool::common::yaml::loadFile(file);
            const auto nodes = modelNodes(root, file.absoluteFilePath());
            QVERIFY(!nodes.isEmpty());
            for (const auto &entry : nodes)
            {
                const YAML::Node &model = entry.second;
                const YAML::Node train_params = model["train_params"];
                const YAML::Node training = findGroup(train_params, QStringLiteral("training"));
                QVERIFY2(training.IsDefined(), qPrintable(QStringLiteral("%1 缺少 training 参数组").arg(entry.first)));

                // 验收条件 2 & 3: 验证间隔使用键名 'val'，类型为 int，默认 1，范围 [1, 100, 1]，不误改为布尔值，且不得存在别名 val_interval
                QVERIFY2(!hasParameter(training, QStringLiteral("val_interval")),
                         qPrintable(QStringLiteral("%1 不得存在废弃别名 val_interval").arg(entry.first)));
                const YAML::Node val_node = findParameter(training, QStringLiteral("val"));
                QVERIFY2(val_node.IsDefined(), qPrintable(QStringLiteral("%1 缺少 val 参数").arg(entry.first)));
                QCOMPARE(val_node["value_type"].as<std::string>(), std::string("int"));
                QCOMPARE(val_node["value"].as<int>(), 1);
                QVERIFY(val_node["value_range"].IsSequence());
                QCOMPARE(val_node["value_range"].size(), 3);
                QCOMPARE(val_node["value_range"][0].as<int>(), 1);
                QCOMPARE(val_node["value_range"][1].as<int>(), 100);
                QCOMPARE(val_node["value_range"][2].as<int>(), 1);

                // 验收条件 3: batch 默认 8，范围 [1, 512, 1]，不包含 auto
                const YAML::Node batch_node = findParameter(training, QStringLiteral("batch"));
                QVERIFY2(batch_node.IsDefined(), qPrintable(QStringLiteral("%1 缺少 batch 参数").arg(entry.first)));
                QCOMPARE(batch_node["value"].as<int>(), 8);
                QCOMPARE(batch_node["value_type"].as<std::string>(), std::string("int"));
                QVERIFY(batch_node["value_range"].IsSequence());
                QCOMPARE(batch_node["value_range"][0].as<int>(), 1);
                QCOMPARE(batch_node["value_range"][1].as<int>(), 512);

                // 验收条件 3: workers 默认 2，上限 128，范围 [0, 128, 1]
                const YAML::Node workers_node = findParameter(training, QStringLiteral("workers"));
                QVERIFY2(workers_node.IsDefined(), qPrintable(QStringLiteral("%1 缺少 workers 参数").arg(entry.first)));
                QCOMPARE(workers_node["value"].as<int>(), 2);
                QCOMPARE(workers_node["value_type"].as<std::string>(), std::string("int"));
                QVERIFY(workers_node["value_range"].IsSequence());
                QCOMPARE(workers_node["value_range"][0].as<int>(), 0);
                QCOMPARE(workers_node["value_range"][1].as<int>(), 128);

                // 优化器不得包含 auto
                const YAML::Node opt_node = findParameter(training, QStringLiteral("optimizer"));
                QVERIFY2(opt_node.IsDefined(), qPrintable(QStringLiteral("%1 缺少 optimizer 参数").arg(entry.first)));
                QCOMPARE(opt_node["value"].as<std::string>(), std::string("AdamW"));
                if (opt_node["options"] && opt_node["options"].IsSequence())
                {
                    for (const YAML::Node &opt : opt_node["options"])
                    {
                        QVERIFY(opt.as<std::string>() != "auto");
                    }
                }
            }
        }

        // 2. 验证 Python 端消费契约：train_impl.py 中 TRAIN_KWARG_WHITELIST 包含 val 且不包含 val_interval，
        // 且直接使用 key 传递而不做 val_interval 别名重映射
        const QString train_impl_path = dltool::common::runtimePath(
            QStringLiteral("3rdparty/EasyTrain/src/python/ultralytics/ultralytics/train_impl.py"));
        QFile train_impl_file(train_impl_path);
        QVERIFY2(train_impl_file.open(QIODevice::ReadOnly), qPrintable(train_impl_file.errorString()));
        const QString content = QString::fromUtf8(train_impl_file.readAll());
        train_impl_file.close();

        QVERIFY2(content.contains(QStringLiteral("\"val\",")), "train_impl.py 白名单中必须包含 \"val\"");
        QVERIFY2(!content.contains(QStringLiteral("\"val_interval\",")),
                 "train_impl.py 白名单中不得存在废弃别名 \"val_interval\"");
        QVERIFY2(!content.contains(QStringLiteral("\"val\" if key == \"val_interval\"")),
                 "train_impl.py 不得保留 val_interval 别名补丁逻辑");

        // 3. 验证持久化层与界面、框架入参使用同一键名 val (不使用别名 val_interval)
        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());
        const QString model_db_path = QDir(temp_dir.path()).filePath(QStringLiteral("model.db"));
        dltool::database::ModelDataBase model_db(model_db_path);

        QVariantMap training_group;
        training_group.insert(QStringLiteral("val"), 5);
        training_group.insert(QStringLiteral("batch"), 8);
        training_group.insert(QStringLiteral("workers"), 2);
        QVariantMap train_params_map;
        train_params_map.insert(QStringLiteral("training"), training_group);

        QString db_error;
        QVERIFY2(model_db.replaceTrainParams(train_params_map, &db_error), qPrintable(db_error));

        QVariantMap read_back;
        QVERIFY2(model_db.readTrainParams(read_back, &db_error), qPrintable(db_error));
        const QVariantMap read_training = read_back.value(QStringLiteral("training")).toMap();
        QVERIFY(!read_training.contains(QStringLiteral("val_interval")));
        QCOMPARE(read_training.value(QStringLiteral("val")).toInt(), 5);
        QCOMPARE(read_training.value(QStringLiteral("batch")).toInt(), 8);
        QCOMPARE(read_training.value(QStringLiteral("workers")).toInt(), 2);
    }

    void anomalyAndInternalModelParametersMatchExecutionAndPersistenceContracts()
    {
        // 1. 验证 PatchCore 模型参数及执行链
        const QString patchcore_file = dltool::common::runtimePath(
            QStringLiteral("config/models/anomalib/patchcore.yaml"));
        QVERIFY2(QFileInfo::exists(patchcore_file), "patchcore.yaml 不存在");
        {
            const YAML::Node root = dltool::common::yaml::loadFile(QFileInfo(patchcore_file));
            const auto nodes = modelNodes(root, patchcore_file);
            QCOMPARE(nodes.size(), 1);
            const YAML::Node &model = nodes.front().second;
            QCOMPARE(model["method"].as<std::string>(), std::string("AnomalyDetection"));
            QCOMPARE(model["framework"].as<std::string>(), std::string("anomalib"));
            QCOMPARE(model["model_architecture"].as<std::string>(), std::string("patchcore"));

            const YAML::Node train_params = model["train_params"];
            const YAML::Node network = findGroup(train_params, QStringLiteral("network"));
            QVERIFY(network.IsDefined());
            const YAML::Node training = findGroup(train_params, QStringLiteral("training"));
            QVERIFY(training.IsDefined());

            // network 参数核对
            const YAML::Node image_size = findParameter(network, QStringLiteral("image_size"));
            QVERIFY(image_size.IsDefined());
            QCOMPARE(image_size["value_type"].as<std::string>(), std::string("int"));
            QCOMPARE(image_size["value"].as<int>(), 256);

            const YAML::Node backbone = findParameter(network, QStringLiteral("backbone"));
            QVERIFY(backbone.IsDefined());
            QCOMPARE(backbone["value"].as<std::string>(), std::string("wide_resnet50_2"));

            const YAML::Node center_crop = findParameter(network, QStringLiteral("center_crop_size"));
            QVERIFY(center_crop.IsDefined());
            QCOMPARE(center_crop["value_type"].as<std::string>(), std::string("int"));
            QCOMPARE(center_crop["value"].as<int>(), 0);

            // training 必须保留产品默认：batch 8, workers 2 且范围 [0, 128, 1]
            const YAML::Node batch = findParameter(training, QStringLiteral("batch_size"));
            QVERIFY(batch.IsDefined());
            QCOMPARE(batch["value"].as<int>(), 8);
            const YAML::Node workers = findParameter(training, QStringLiteral("num_workers"));
            QVERIFY(workers.IsDefined());
            QCOMPARE(workers["value"].as<int>(), 2);
            QCOMPARE(workers["value_range"][1].as<int>(), 128);

            // test_params 分组与参数核对：inference vs evaluation
            const YAML::Node test_params = model["test_params"];
            const YAML::Node inference = findGroup(test_params, QStringLiteral("inference"));
            QVERIFY(inference.IsDefined());
            const YAML::Node evaluation = findGroup(test_params, QStringLiteral("evaluation"));
            QVERIFY(evaluation.IsDefined());

            // inference 必须有 batch_size, num_workers, checkpoint
            QVERIFY(findParameter(inference, QStringLiteral("batch_size")).IsDefined());
            QCOMPARE(findParameter(inference, QStringLiteral("batch_size"))["value"].as<int>(), 8);
            QVERIFY(findParameter(inference, QStringLiteral("num_workers")).IsDefined());
            QCOMPARE(findParameter(inference, QStringLiteral("num_workers"))["value"].as<int>(), 2);
            const YAML::Node ckpt = findParameter(inference, QStringLiteral("checkpoint"));
            QVERIFY(ckpt.IsDefined());
            QCOMPARE(ckpt["model_param_name"].as<std::string>(), std::string("backbone"));

            // evaluation 必须有 classification_threshold 和 heatmap_threshold，且类型与默认值正确
            const YAML::Node cls_thresh = findParameter(evaluation, QStringLiteral("classification_threshold"));
            QVERIFY(cls_thresh.IsDefined());
            QCOMPARE(cls_thresh["value_type"].as<std::string>(), std::string("double"));
            QCOMPARE(cls_thresh["value"].as<double>(), 0.5);

            const YAML::Node heat_thresh = findParameter(evaluation, QStringLiteral("heatmap_threshold"));
            QVERIFY(heat_thresh.IsDefined());
            QCOMPARE(heat_thresh["value_type"].as<std::string>(), std::string("double"));
            QCOMPARE(heat_thresh["value"].as<double>(), 1.0);
        }

        // 2. 验证 Anomalib Dinomaly2 模型参数
        const QString anomalib_dinomaly_file = dltool::common::runtimePath(
            QStringLiteral("config/models/anomalib/dinomaly2.yaml"));
        QVERIFY2(QFileInfo::exists(anomalib_dinomaly_file), "anomalib/dinomaly2.yaml 不存在");
        {
            const YAML::Node root = dltool::common::yaml::loadFile(QFileInfo(anomalib_dinomaly_file));
            const auto nodes = modelNodes(root, anomalib_dinomaly_file);
            QCOMPARE(nodes.size(), 1);
            const YAML::Node &model = nodes.front().second;
            const YAML::Node train_params = model["train_params"];
            const YAML::Node training = findGroup(train_params, QStringLiteral("training"));
            QVERIFY(training.IsDefined());
            QCOMPARE(findParameter(training, QStringLiteral("batch_size"))["value"].as<int>(), 8);
            QCOMPARE(findParameter(training, QStringLiteral("num_workers"))["value"].as<int>(), 2);

            const YAML::Node test_params = model["test_params"];
            const YAML::Node evaluation = findGroup(test_params, QStringLiteral("evaluation"));
            QVERIFY(evaluation.IsDefined());
            QCOMPARE(findParameter(evaluation, QStringLiteral("classification_threshold"))["value"].as<double>(), 0.5);
            QCOMPARE(findParameter(evaluation, QStringLiteral("heatmap_threshold"))["value"].as<double>(), 1.0);
        }

        // 3. 验证独立 Dinomaly2 (Mask 约束训练) 模型参数及 mask 组
        const QString dinomaly_standalone_file = dltool::common::runtimePath(
            QStringLiteral("config/models/dinomaly2/dinomaly2.yaml"));
        QVERIFY2(QFileInfo::exists(dinomaly_standalone_file), "dinomaly2/dinomaly2.yaml 不存在");
        {
            const YAML::Node root = dltool::common::yaml::loadFile(QFileInfo(dinomaly_standalone_file));
            const auto nodes = modelNodes(root, dinomaly_standalone_file);
            QCOMPARE(nodes.size(), 1);
            const YAML::Node &model = nodes.front().second;
            const YAML::Node train_params = model["train_params"];
            const YAML::Node training = findGroup(train_params, QStringLiteral("training"));
            QVERIFY(training.IsDefined());
            QCOMPARE(findParameter(training, QStringLiteral("batch_size"))["value"].as<int>(), 8);
            QCOMPARE(findParameter(training, QStringLiteral("num_workers"))["value"].as<int>(), 2);

            const YAML::Node mask_group = findGroup(train_params, QStringLiteral("mask"));
            QVERIFY(mask_group.IsDefined());
            const YAML::Node good_val = findParameter(mask_group, QStringLiteral("good_value"));
            QVERIFY(good_val.IsDefined());
            QCOMPARE(good_val["value_type"].as<std::string>(), std::string("string"));
            QCOMPARE(good_val["value"].as<std::string>(), std::string("1"));

            const YAML::Node anomaly_val = findParameter(mask_group, QStringLiteral("anomaly_value"));
            QVERIFY(anomaly_val.IsDefined());
            QCOMPARE(anomaly_val["value_type"].as<std::string>(), std::string("string"));
            QCOMPARE(anomaly_val["value"].as<std::string>(), std::string("255"));

            const YAML::Node ignore_val = findParameter(mask_group, QStringLiteral("ignore_value"));
            QVERIFY(ignore_val.IsDefined());
            QCOMPARE(ignore_val["value_type"].as<std::string>(), std::string("string"));
            QCOMPARE(ignore_val["value"].as<std::string>(), std::string("254"));

            const YAML::Node hflip = findParameter(mask_group, QStringLiteral("aug_hflip_prob"));
            QVERIFY(hflip.IsDefined());
            QCOMPARE(hflip["value_type"].as<std::string>(), std::string("double"));
            QCOMPARE(hflip["value"].as<double>(), 0.0);

            const YAML::Node test_params = model["test_params"];
            const YAML::Node evaluation = findGroup(test_params, QStringLiteral("evaluation"));
            QVERIFY(evaluation.IsDefined());
            QCOMPARE(findParameter(evaluation, QStringLiteral("classification_threshold"))["value"].as<double>(), 0.5);
            QCOMPARE(findParameter(evaluation, QStringLiteral("heatmap_threshold"))["value"].as<double>(), 1.0);
        }

        // 4. 验证 FS-SAM2 内部流程契约：无普通模型 evaluation 组，保持小样本能力位及内部流程
        const QString fs_sam2_file = dltool::common::runtimePath(
            QStringLiteral("config/models/FS-SAM2/FS-SAM2.yaml"));
        QVERIFY2(QFileInfo::exists(fs_sam2_file), "FS-SAM2.yaml 不存在");
        {
            const YAML::Node root = dltool::common::yaml::loadFile(QFileInfo(fs_sam2_file));
            const auto nodes = modelNodes(root, fs_sam2_file);
            QCOMPARE(nodes.size(), 1);
            const YAML::Node &model = nodes.front().second;
            QCOMPARE(model["method"].as<std::string>(), std::string("FewShotLearning"));

            const YAML::Node train_params = model["train_params"];
            const YAML::Node training = findGroup(train_params, QStringLiteral("training"));
            QVERIFY(training.IsDefined());
            QCOMPARE(findParameter(training, QStringLiteral("batch_size"))["value"].as<int>(), 8);
            QCOMPARE(findParameter(training, QStringLiteral("num_workers"))["value"].as<int>(), 2);

            const YAML::Node test_params = model["test_params"];
            QVERIFY(test_params.IsDefined());
            // 验收条件 2: FS-SAM2 保持内部流程，不得定义普通模型 evaluation 组
            QVERIFY(!hasGroup(test_params, QStringLiteral("evaluation")));
            QVERIFY(hasGroup(test_params, QStringLiteral("inference")));
            QVERIFY(hasGroup(test_params, QStringLiteral("model")));

            // 验证 C++ ModelRegistry 中的 FS-SAM2 框架定义
            const auto fs_sam2_framework = dltool::model::registeredFramework(
                dltool::core::DeepLearningMethod::Detection, QStringLiteral("FS-SAM2"));
            QVERIFY(fs_sam2_framework.isFewShot());
            QVERIFY(!fs_sam2_framework.visible_for_model_creation);
            QCOMPARE(fs_sam2_framework.default_test_task_directory, QStringLiteral("fs_sam2"));
        }

        // 5. 验证 Python 端消费契约与 evaluation 隔离契约
        // Anomalib 的 dltool_common.py 中 load_database_config 严格隔离 evaluation，只传递 inference
        const QString anomalib_common_path = dltool::common::runtimePath(
            QStringLiteral("3rdparty/EasyTrain/src/python/open-edge-platform/anomalib/dltool_common.py"));
        QFile anomalib_common_file(anomalib_common_path);
        QVERIFY2(anomalib_common_file.open(QIODevice::ReadOnly), qPrintable(anomalib_common_file.errorString()));
        const QString anomalib_common_content = QString::fromUtf8(anomalib_common_file.readAll());
        anomalib_common_file.close();

        QVERIFY2(anomalib_common_content.contains(
                     QStringLiteral("test_params = {\"inference\": dict(group({\"test_params\": raw_test_params}, \"test_params\", \"inference\"))}")),
                 "Anomalib Python 端必须严格隔离 evaluation 参数，仅接收 inference 参数");
        QVERIFY2(anomalib_common_content.contains(
                     QStringLiteral("batch_size = integer(runtime_params, \"batch_size\", 8)")),
                 "Anomalib dataloader batch_size 默认值必须与产品默认 8 一致");
        QVERIFY2(anomalib_common_content.contains(
                     QStringLiteral("num_workers=integer(runtime_params, \"num_workers\", 2)")),
                 "Anomalib dataloader num_workers 默认值必须与产品默认 2 一致");

        // 独立 Dinomaly2 的 dltool_common.py 中同样严格隔离 evaluation
        const QString dinomaly_common_path = dltool::common::runtimePath(
            QStringLiteral("3rdparty/EasyTrain/src/python/guojiajeremy/Dinomaly2/dltool_common.py"));
        QFile dinomaly_common_file(dinomaly_common_path);
        QVERIFY2(dinomaly_common_file.open(QIODevice::ReadOnly), qPrintable(dinomaly_common_file.errorString()));
        const QString dinomaly_common_content = QString::fromUtf8(dinomaly_common_file.readAll());
        dinomaly_common_file.close();

        QVERIFY2(dinomaly_common_content.contains(
                     QStringLiteral("test_params = {\"inference\": dict(group({\"test_params\": raw_test_params}, \"test_params\", \"inference\"))}")),
                 "Dinomaly2 Python 端必须严格隔离 evaluation 参数，仅接收 inference 参数");

        // Dinomaly2 的 train_impl.py 和 predict_impl.py 中的 num_workers 默认值回退为 2
        const QString dinomaly_train_path = dltool::common::runtimePath(
            QStringLiteral("3rdparty/EasyTrain/src/python/guojiajeremy/Dinomaly2/train_impl.py"));
        QFile dinomaly_train_file(dinomaly_train_path);
        QVERIFY2(dinomaly_train_file.open(QIODevice::ReadOnly), qPrintable(dinomaly_train_file.errorString()));
        const QString dinomaly_train_content = QString::fromUtf8(dinomaly_train_file.readAll());
        dinomaly_train_file.close();
        QVERIFY2(dinomaly_train_content.contains(
                     QStringLiteral("num_workers = integer(training, \"num_workers\", 2)")),
                 "Dinomaly2 train_impl.py num_workers 回退值必须与产品默认 2 一致");

        const QString dinomaly_predict_path = dltool::common::runtimePath(
            QStringLiteral("3rdparty/EasyTrain/src/python/guojiajeremy/Dinomaly2/predict_impl.py"));
        QFile dinomaly_predict_file(dinomaly_predict_path);
        QVERIFY2(dinomaly_predict_file.open(QIODevice::ReadOnly), qPrintable(dinomaly_predict_file.errorString()));
        const QString dinomaly_predict_content = QString::fromUtf8(dinomaly_predict_file.readAll());
        dinomaly_predict_file.close();
        QVERIFY2(dinomaly_predict_content.contains(
                     QStringLiteral("num_workers = integer(inference, \"num_workers\", 2)")),
                 "Dinomaly2 predict_impl.py num_workers 回退值必须与产品默认 2 一致");

        // 6. 验证持久化层 ModelDataBase 与 ModelTaskDataBase 对异常检测参数完整生命周期的支持
        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());
        const QString model_db_path = QDir(temp_dir.path()).filePath(QStringLiteral("model.db"));
        const QString task_db_path = QDir(temp_dir.path()).filePath(QStringLiteral("task.db"));

        // 6.1 ModelDataBase 存储与读取 train_params (含 network, training, mask)
        dltool::database::ModelDataBase model_db(model_db_path);
        QVariantMap train_params_map;
        QVariantMap net_group;
        net_group.insert(QStringLiteral("image_size"), 256);
        net_group.insert(QStringLiteral("backbone"), QStringLiteral("wide_resnet50_2"));
        train_params_map.insert(QStringLiteral("network"), net_group);

        QVariantMap trn_group;
        trn_group.insert(QStringLiteral("batch_size"), 8);
        trn_group.insert(QStringLiteral("num_workers"), 2);
        train_params_map.insert(QStringLiteral("training"), trn_group);

        QVariantMap mask_grp;
        mask_grp.insert(QStringLiteral("good_value"), QStringLiteral("1"));
        mask_grp.insert(QStringLiteral("anomaly_value"), QStringLiteral("255"));
        train_params_map.insert(QStringLiteral("mask"), mask_grp);

        QString db_error;
        QVERIFY2(model_db.replaceTrainParams(train_params_map, &db_error), qPrintable(db_error));

        QVariantMap read_train;
        QVERIFY2(model_db.readTrainParams(read_train, &db_error), qPrintable(db_error));
        QCOMPARE(read_train.value(QStringLiteral("network")).toMap().value(QStringLiteral("image_size")).toInt(), 256);
        QCOMPARE(read_train.value(QStringLiteral("network")).toMap().value(QStringLiteral("backbone")).toString(),
                 QStringLiteral("wide_resnet50_2"));
        QCOMPARE(read_train.value(QStringLiteral("training")).toMap().value(QStringLiteral("batch_size")).toInt(), 8);
        QCOMPARE(read_train.value(QStringLiteral("training")).toMap().value(QStringLiteral("num_workers")).toInt(), 2);
        QCOMPARE(read_train.value(QStringLiteral("mask")).toMap().value(QStringLiteral("good_value")).toString(),
                 QStringLiteral("1"));
        QCOMPARE(read_train.value(QStringLiteral("mask")).toMap().value(QStringLiteral("anomaly_value")).toString(),
                 QStringLiteral("255"));

        // 6.2 ModelTaskDataBase 存储与读取 test_params (含 inference 与 evaluation)
        dltool::database::ModelTaskDataBase task_db(task_db_path);
        QVariantMap test_params_map;
        QVariantMap inf_group;
        inf_group.insert(QStringLiteral("batch_size"), 8);
        inf_group.insert(QStringLiteral("num_workers"), 2);
        inf_group.insert(QStringLiteral("checkpoint"), QStringLiteral(""));
        test_params_map.insert(QStringLiteral("inference"), inf_group);

        QVariantMap eval_group;
        eval_group.insert(QStringLiteral("classification_threshold"), 0.5);
        eval_group.insert(QStringLiteral("heatmap_threshold"), 1.0);
        test_params_map.insert(QStringLiteral("evaluation"), eval_group);

        QVERIFY2(task_db.replaceTestParams(test_params_map, &db_error), qPrintable(db_error));

        QVariantMap read_test;
        QVERIFY2(task_db.readTestParams(read_test, &db_error), qPrintable(db_error));
        const QVariantMap read_inf = read_test.value(QStringLiteral("inference")).toMap();
        QCOMPARE(read_inf.value(QStringLiteral("batch_size")).toInt(), 8);
        QCOMPARE(read_inf.value(QStringLiteral("num_workers")).toInt(), 2);

        const QVariantMap read_eval = read_test.value(QStringLiteral("evaluation")).toMap();
        QCOMPARE(read_eval.value(QStringLiteral("classification_threshold")).toDouble(), 0.5);
        QCOMPARE(read_eval.value(QStringLiteral("heatmap_threshold")).toDouble(), 1.0);
    }
};

REGISTER_TEST(ModelConfigConsistencyTest)

#include "test_ModelConfigConsistency.moc"

