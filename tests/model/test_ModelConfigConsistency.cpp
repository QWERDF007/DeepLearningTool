#include "../test_runner.h"

#include "common/Utils.h"
#include "common/YamlUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTest>

#include <yaml-cpp/yaml.h>

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
};

REGISTER_TEST(ModelConfigConsistencyTest)

#include "test_ModelConfigConsistency.moc"
