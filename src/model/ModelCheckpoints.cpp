#include "model/ModelCheckpoints.h"

#include "database/DataBase.h"
#include "database/ModelDataBase.h"
#include "model/ModelRegistry.h"
#include "model/ModelStorageService.h"
#include "parameter/DynamicOptions.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <QMetaType>
#include <QVariantList>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace dltool::model {

namespace {

constexpr const char *kProviderKey     = "model.checkpoints";
constexpr const char *kContextModel            = "model_name";
constexpr const char *kContextProject          = "project_dir";
constexpr const char *kContextProjectDatabase  = "project_db";
constexpr const char *kContextFramework        = "framework_name";
constexpr const char *kContextArchitecture     = "model_architecture";
constexpr const char *kContextExts             = "extensions";
constexpr const char *kContextModelParam        = "model_param_name";
constexpr const char *kContextModelParamValue   = "model_param_value";
constexpr const char *kContextOfficialModel     = "official_model";
constexpr const char *kContextOfficialWeight    = "official_weight";
constexpr const char *kContextCurrentValue      = "current_value";

struct ModelEntry
{
    QString name;
    QString framework_name;
    QString architecture;
};

struct WeightParts
{
    QString model_name;
    QString weight_name;
    QString path;
};

bool sameText(const QString &lhs, const QString &rhs)
{
    return lhs.trimmed().compare(rhs.trimmed(), Qt::CaseInsensitive) == 0;
}

bool matchesMetadata(const QString &actual, const QString &expected)
{
    return expected.trimmed().isEmpty() || sameText(actual, expected);
}

bool sameOptionValue(const QVariant &lhs, const QVariant &rhs)
{
    const QString left  = lhs.toString().trimmed();
    const QString right = rhs.toString().trimmed();
    if (left.compare(right, Qt::CaseInsensitive) == 0)
        return true;

    if (!left.contains(QDir::separator()) && !right.contains(QDir::separator()) && !left.contains('/')
        && !right.contains('/'))
    {
        return QFileInfo(left).completeBaseName().compare(QFileInfo(right).completeBaseName(), Qt::CaseInsensitive)
            == 0;
    }
    return QDir::cleanPath(left).compare(QDir::cleanPath(right), Qt::CaseInsensitive) == 0;
}

QString normalizedExtension(const QString &extension)
{
    QString result = extension.trimmed();
    if (!result.isEmpty() && !result.startsWith('.'))
        result.prepend('.');
    return result;
}

QString officialModelName(const QString &model_value, const QString &framework_name, const QString &architecture)
{
    const QString value = model_value.trimmed();
    if (value.isEmpty() || !sameText(framework_name, QStringLiteral("ultralytics")))
        return value;

    const QString stem = QFileInfo(value).completeBaseName();
    const QString prefix = architecture.trimmed().toLower().remove(QStringLiteral("-seg"));
    const QStringList variants = {QStringLiteral("n"), QStringLiteral("s"), QStringLiteral("m"),
                                  QStringLiteral("l"), QStringLiteral("x")};
    if (!prefix.isEmpty() && variants.contains(stem, Qt::CaseInsensitive))
        return prefix + stem.toLower() + (architecture.trimmed().endsWith(QStringLiteral("-seg"), Qt::CaseInsensitive)
                                              ? QStringLiteral("-seg")
                                              : QString());
    return value;
}

QString canonicalModelParameterValue(const QString &value, const QString &framework_name,
                                     const QString &architecture)
{
    const QString normalized = officialModelName(value, framework_name, architecture);
    if (sameText(framework_name, QStringLiteral("ultralytics")))
        return QFileInfo(normalized).completeBaseName().toLower();
    return normalized.trimmed().toLower();
}

bool sameModelParameterValue(const QString &lhs, const QString &rhs, const QString &framework_name,
                             const QString &architecture)
{
    return sameText(lhs, rhs)
        || canonicalModelParameterValue(lhs, framework_name, architecture)
               == canonicalModelParameterValue(rhs, framework_name, architecture);
}

QString officialWeightValue(const QString &model_value, const QString &framework_name, const QString &architecture,
                            const QStringList &extensions)
{
    QString result = officialModelName(model_value, framework_name, architecture);
    if (result.isEmpty())
        return {};
    if (QFileInfo(result).suffix().isEmpty() && !extensions.isEmpty())
        result += normalizedExtension(extensions.front());
    return result;
}

QString collapseDuplicateLabel(const QString &value)
{
    const QString trimmed = value.trimmed();
    const QStringList parts = trimmed.split(QStringLiteral(" / "));
    if (parts.size() < 2 || parts.size() % 2 != 0)
        return trimmed;

    const int half = parts.size() / 2;
    for (int i = 0; i < half; ++i)
    {
        if (parts.at(i) != parts.at(i + half))
            return trimmed;
    }
    return parts.mid(0, half).join(QStringLiteral(" / "));
}

QStringList contextExtensions(const QVariantMap &context)
{
    QStringList extensions;
    const QVariant value = context.value(QString::fromLatin1(kContextExts));
    if (value.userType() == QMetaType::QStringList)
    {
        for (const QString &entry : value.toStringList())
        {
            const QString extension = normalizedExtension(entry);
            if (!extension.isEmpty() && !extensions.contains(extension, Qt::CaseInsensitive))
                extensions.append(extension);
        }
    }
    else if (value.userType() == QMetaType::QVariantList)
    {
        for (const QVariant &entry : value.toList())
        {
            const QString extension = normalizedExtension(entry.toString());
            if (!extension.isEmpty() && !extensions.contains(extension, Qt::CaseInsensitive))
                extensions.append(extension);
        }
    }
    return extensions;
}

QVariantMap modelEntryMap(const QString &label, const QVariant &value, const QVariantList &sub_options,
                          const QString &display_label = {})
{
    QVariantMap entry;
    entry.insert(QStringLiteral("label"), label);
    entry.insert(QStringLiteral("value"), value);
    entry.insert(QStringLiteral("subOptions"), sub_options);
    if (!display_label.trimmed().isEmpty())
        entry.insert(QStringLiteral("displayLabel"), display_label.trimmed());
    return entry;
}

QVariantMap weightEntryMap(const QString &label, const QString &path)
{
    return modelEntryMap(label, path, {});
}

QVariantMap hiddenModelEntryMap(const QString &label, const QVariant &value, const QVariantList &sub_options)
{
    QVariantMap entry = modelEntryMap(label, value, sub_options);
    entry.insert(QStringLiteral("visible"), false);
    return entry;
}

QString modelParameterFromTrainParams(const QVariantMap &train_params, const QString &parameter_name)
{
    QStringList names;
    const QString name = parameter_name.trimmed();
    if (name.isEmpty())
        return {};
    names.append(name);
    if (name.compare(QStringLiteral("model"), Qt::CaseInsensitive) == 0)
        names.append(QStringLiteral("model_size"));
    else if (name.compare(QStringLiteral("model_size"), Qt::CaseInsensitive) == 0)
        names.append(QStringLiteral("model"));
    else if (name.compare(QStringLiteral("backbone"), Qt::CaseInsensitive) == 0)
        names.append(QStringLiteral("encoder_name"));
    else if (name.compare(QStringLiteral("encoder_name"), Qt::CaseInsensitive) == 0)
        names.append(QStringLiteral("backbone"));

    for (const QString &candidate : names)
    {
        if (train_params.contains(candidate))
            return train_params.value(candidate).toString().trimmed();

        for (auto iterator = train_params.cbegin(); iterator != train_params.cend(); ++iterator)
        {
            const QVariantMap group = iterator.value().toMap();
            if (group.contains(candidate))
                return group.value(candidate).toString().trimmed();
        }
    }
    return {};
}

QString modelParameterFromDatabase(const QString &project_dir, const QString &model_name,
                                   const QString &parameter_name)
{
    if (project_dir.trimmed().isEmpty() || model_name.trimmed().isEmpty() || parameter_name.trimmed().isEmpty())
        return {};

    const ModelStorageService storage(project_dir);
    const QString             database_path = storage.modelDatabasePath(model_name);
    if (!QFileInfo::exists(database_path))
        return {};

    database::ModelDataBase model_database(database_path);
    QVariantMap             train_params;
    QString                 error;
    if (!model_database.readTrainParams(train_params, &error))
        return {};
    return modelParameterFromTrainParams(train_params, parameter_name);
}

QVector<ModelEntry> candidateModels(const QString &project_db, const QString &project_dir,
                                    const QString &fallback_framework, const QString &fallback_architecture)
{
    QVector<ModelEntry> models;
    if (!project_db.trimmed().isEmpty() && QFileInfo::exists(project_db))
    {
        database::ProjectDataBase project_database(project_db);
        std::vector<int64_t>      model_ids;
        std::vector<QString>      uuids;
        std::vector<QString>      names;
        std::vector<QString>      framework_names;
        std::vector<QString>      architectures;
        std::vector<qint64>       ctimes;
        std::vector<qint64>       mtimes;
        std::vector<std::vector<uint8_t>> extra_data;
        QString                   error;
        if (project_database.getAllModels(model_ids, uuids, names, framework_names, architectures, ctimes, mtimes,
                                          extra_data, error))
        {
            const size_t count = std::min({names.size(), framework_names.size(), architectures.size()});
            models.reserve(static_cast<int>(count));
            for (size_t i = 0; i < count; ++i)
                models.push_back({names[i], framework_names[i], architectures[i]});
        }
        else
        {
            spdlog::warn("读取项目模型列表失败: {}", error.toUtf8().constData());
        }
        return models;
    }

    if (project_dir.trimmed().isEmpty())
        return models;

    const QDir models_root(QDir(project_dir).filePath(QStringLiteral("models")));
    if (!models_root.exists())
        return models;

    const QFileInfoList entries = models_root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries)
        models.push_back({entry.fileName(), fallback_framework, fallback_architecture});
    return models;
}

QStringList enumerateWeightFiles(const QString &project_dir, const QString &model_name,
                                 const QStringList &extensions)
{
    if (project_dir.trimmed().isEmpty() || model_name.trimmed().isEmpty())
        return {};

    const ModelStorageService storage(project_dir);
    const QDir                dir(storage.trainWeightsPath(model_name));
    if (!dir.exists())
        return {};

    QStringList files;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &entry : entries)
    {
        const QString suffix = normalizedExtension(entry.suffix());
        if (extensions.contains(suffix, Qt::CaseInsensitive))
            files.append(entry.absoluteFilePath());
    }
    return files;
}

WeightParts weightPartsFromPath(const QString &value)
{
    const QFileInfo info(value.trimmed());
    if (!info.isAbsolute())
        return {};

    QDir weights_dir = info.dir();
    if (!sameText(weights_dir.dirName(), QStringLiteral("weights")))
        return {};
    if (!weights_dir.cdUp() || !sameText(weights_dir.dirName(), QStringLiteral("train")))
        return {};
    if (!weights_dir.cdUp())
        return {};

    return {weights_dir.dirName(), info.completeBaseName(), info.absoluteFilePath()};
}

WeightParts weightPartsFromLegacyLabel(const QString &project_dir, const QString &value,
                                       const QStringList &extensions)
{
    const QString trimmed = collapseDuplicateLabel(value);
    const int     separator = trimmed.indexOf(QStringLiteral(" / "));
    if (separator <= 0)
        return {};

    const QString model_name  = trimmed.left(separator).trimmed();
    const QString weight_name = trimmed.mid(separator + 3).trimmed();
    if (model_name.isEmpty() || weight_name.isEmpty())
        return {};

    const ModelStorageService storage(project_dir);
    const QDir                weights_dir(storage.trainWeightsPath(model_name));
    if (!weights_dir.exists())
        return {model_name, QFileInfo(weight_name).completeBaseName(), {}};

    const QFileInfoList entries = weights_dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &entry : entries)
    {
        const bool same_name = sameText(entry.fileName(), weight_name)
            || sameText(entry.completeBaseName(), QFileInfo(weight_name).completeBaseName());
        if (same_name && extensions.contains(normalizedExtension(entry.suffix()), Qt::CaseInsensitive))
            return {model_name, entry.completeBaseName(), entry.absoluteFilePath()};
    }

    QString path = weight_name;
    if (QFileInfo(path).suffix().isEmpty() && !extensions.isEmpty())
        path += extensions.front();
    return {model_name, QFileInfo(weight_name).completeBaseName(), weights_dir.filePath(path)};
}

WeightParts weightPartsFromValue(const QString &project_dir, const QString &value, const QStringList &extensions)
{
    const WeightParts path_parts = weightPartsFromPath(value);
    if (!path_parts.model_name.isEmpty())
        return path_parts;
    return weightPartsFromLegacyLabel(project_dir, collapseDuplicateLabel(value), extensions);
}

QString effectiveCurrentValue(const QString &project_dir, const QString &value, const QStringList &extensions)
{
    const WeightParts parts = weightPartsFromValue(project_dir, value, extensions);
    return parts.path.isEmpty() ? collapseDuplicateLabel(value) : parts.path;
}

QString currentDisplayLabel(const QString &project_dir, const QString &value, const QStringList &extensions)
{
    const WeightParts parts = weightPartsFromValue(project_dir, value, extensions);
    if (!parts.model_name.isEmpty() && !parts.weight_name.isEmpty())
        return parts.model_name + QStringLiteral(" / ") + parts.weight_name;
    return collapseDuplicateLabel(value);
}

class ModelCheckpointsProvider final : public parameter::DynamicOptionsProvider
{
public:
    QString key() const override { return QString::fromLatin1(kProviderKey); }

    parameter::DynamicOptionsData query(const QVariantMap &context) const override
    {
        parameter::DynamicOptionsData result;

        const QString project_dir       = context.value(QString::fromLatin1(kContextProject)).toString().trimmed();
        const QString project_db        = context.value(QString::fromLatin1(kContextProjectDatabase)).toString().trimmed();
        const QString current_model     = context.value(QString::fromLatin1(kContextModel)).toString().trimmed();
        const QString framework_name    = context.value(QString::fromLatin1(kContextFramework)).toString().trimmed();
        const QString architecture      = context.value(QString::fromLatin1(kContextArchitecture)).toString().trimmed();
        const QString model_param_name  = context.value(QString::fromLatin1(kContextModelParam)).toString().trimmed();
        QString       model_param_value = context.value(QString::fromLatin1(kContextModelParamValue)).toString().trimmed();
        QString       official_model    = context.value(QString::fromLatin1(kContextOfficialModel)).toString().trimmed();
        const bool    official_weight   = context.value(QString::fromLatin1(kContextOfficialWeight)).toBool();
        const QString current_value     = context.value(QString::fromLatin1(kContextCurrentValue)).toString().trimmed();
        if (project_dir.isEmpty())
            return result;

        const QStringList extensions = contextExtensions(context);
        if (!model_param_name.isEmpty())
        {
            const QString database_value = modelParameterFromDatabase(project_dir, current_model, model_param_name);
            if (model_param_value.isEmpty())
                model_param_value = database_value;
            if (official_model.isEmpty())
                official_model = database_value;
        }

        auto appendFlatOption = [&result](const QString &label, const QVariant &value)
        {
            if (label.trimmed().isEmpty())
                return;
            for (const auto &option : result.options)
            {
                if (sameOptionValue(option.actual_value, value))
                    return;
            }
            result.options.push_back({label, value});
        };

        if (official_weight && !official_model.isEmpty())
        {
            const QString official_value
                = officialWeightValue(official_model, framework_name, architecture, extensions);
            result.option_groups.append(
                modelEntryMap(QStringLiteral("官方权重"), official_value, {}, QStringLiteral("官方模型")));
            appendFlatOption(QStringLiteral("官方模型"), official_value);
        }

        const QVector<ModelEntry> models = candidateModels(project_db, project_dir, framework_name, architecture);
        for (const ModelEntry &model : models)
        {
            if (sameText(model.name, current_model))
                continue;

            if (!matchesMetadata(model.framework_name, framework_name)
                || !matchesMetadata(model.architecture, architecture))
                continue;

            if (!model_param_name.isEmpty() && !model_param_value.isEmpty())
            {
                const QString candidate_value
                    = modelParameterFromDatabase(project_dir, model.name, model_param_name);
                if (!sameModelParameterValue(candidate_value, model_param_value, framework_name, architecture))
                    continue;
            }

            const QStringList files = enumerateWeightFiles(project_dir, model.name, extensions);
            if (files.isEmpty())
                continue;

            QVariantList sub_options;
            for (const QString &file : files)
            {
                const QFileInfo info(file);
                const QString   label = info.completeBaseName();
                sub_options.append(weightEntryMap(label, info.absoluteFilePath()));
                appendFlatOption(model.name + QStringLiteral(" / ") + label, info.absoluteFilePath());
            }
            result.option_groups.append(modelEntryMap(model.name, QString(), sub_options));
        }

        if (!current_value.isEmpty())
        {
            const QString current_actual = effectiveCurrentValue(project_dir, current_value, extensions);
            bool          found          = false;
            for (const auto &option : result.options)
            {
                if (sameOptionValue(option.actual_value, current_actual))
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                const WeightParts parts = weightPartsFromValue(project_dir, current_value, extensions);
                if (!parts.model_name.isEmpty() && !parts.weight_name.isEmpty())
                {
                    QVariantList sub_options;
                    sub_options.append(weightEntryMap(parts.weight_name, current_actual));
                    result.option_groups.append(hiddenModelEntryMap(parts.model_name, QString(), sub_options));
                    appendFlatOption(currentDisplayLabel(project_dir, current_value, extensions), current_actual);
                }
                else
                {
                    const QString label = currentDisplayLabel(project_dir, current_value, extensions);
                    result.option_groups.append(hiddenModelEntryMap(label, current_actual, {}));
                    appendFlatOption(label, current_actual);
                }
            }
        }
        return result;
    }
};

[[maybe_unused]] const bool providerRegistered = []
{
    registerModelCheckpointsProvider();
    return true;
}();

} // namespace

void registerModelCheckpointsProvider()
{
    parameter::registerDynamicOptionsProvider(std::make_unique<ModelCheckpointsProvider>());
}

} // namespace dltool::model
