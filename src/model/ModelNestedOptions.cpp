#include "model/ModelNestedOptions.h"

#include "database/DataBase.h"
#include "database/ModelDataBase.h"
#include "model/ModelStorageService.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace dltool::model {

namespace {

struct ModelEntry
{
    QString name;
    QString framework_name;
    QString architecture;
};

bool sameArchitecture(const QString &lhs, const QString &rhs)
{
    return lhs.trimmed().compare(rhs.trimmed(), Qt::CaseInsensitive) == 0;
}

QString officialWeightName(const QStringList &variants, const QString &variant_template,
                           const QString &variant_value)
{
    if (variants.isEmpty() || variant_template.trimmed().isEmpty() || variant_value.trimmed().isEmpty())
        return {};
    if (!variants.contains(variant_value))
        return {};
    QString name = variant_template.trimmed();
    return name.replace(QStringLiteral("{size}"), variant_value.trimmed());
}

QStringList enumerateWeightFiles(const QString &project_dir, const QString &model_name,
                                 const QStringList &extensions)
{
    if (project_dir.trimmed().isEmpty() || model_name.trimmed().isEmpty() || extensions.isEmpty())
        return {};

    const ModelStorageService storage(project_dir);
    const QString             weights_dir = storage.trainWeightsPath(model_name);
    const QDir                dir(weights_dir);
    if (!dir.exists())
        return {};

    QStringList files;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &entry : entries)
    {
        const QString suffix = entry.suffix().toLower();
        if (extensions.contains(QStringLiteral(".") + suffix, Qt::CaseInsensitive))
            files.append(entry.absoluteFilePath());
    }
    return files;
}

QVariantMap modelEntryMap(const QString &label, const QVariant &value, const QVariantList &sub_options)
{
    QVariantMap entry;
    entry.insert(QStringLiteral("label"), label);
    entry.insert(QStringLiteral("value"), value);
    entry.insert(QStringLiteral("subOptions"), sub_options);
    return entry;
}

QString modelSizeFromDatabase(const QString &project_dir, const QString &model_name, const QString &size_param)
{
    if (project_dir.trimmed().isEmpty() || model_name.trimmed().isEmpty() || size_param.trimmed().isEmpty())
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

    const QVariantMap network = train_params.value(QStringLiteral("network")).toMap();
    const QVariant    value   = network.value(size_param);
    return value.isValid() ? value.toString().trimmed() : QString();
}

QVector<ModelEntry> candidateModels(const QString &project_db, const QString &project_dir,
                                    const QString &framework_name, const QString &architecture)
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
            for (size_t i = 0; i < names.size(); ++i)
            {
                if (i < framework_names.size() && i < architectures.size())
                    models.push_back({names[i], framework_names[i], architectures[i]});
            }
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
        models.push_back({entry.fileName(), {}, {}});
    return models;
}

QVariantList buildUserModelOptions(const QString &project_dir, const QString &project_db,
                                   const QString &framework_name, const QString &architecture,
                                   const QStringList &extensions, const QString &size_param,
                                   const QString &variant_value)
{
    QVariantList entries;
    const QVector<ModelEntry> models = candidateModels(project_db, project_dir, framework_name, architecture);
    for (const ModelEntry &model : models)
    {
        if (!sameArchitecture(model.architecture, architecture))
            continue;

        if (!variant_value.trimmed().isEmpty() && !size_param.trimmed().isEmpty())
        {
            const QString model_size = modelSizeFromDatabase(project_dir, model.name, size_param);
            if (model_size.compare(variant_value, Qt::CaseInsensitive) != 0)
                continue;
        }

        const QStringList files = enumerateWeightFiles(project_dir, model.name, extensions);
        if (files.isEmpty())
            continue;

        QVariantList sub_options;
        for (const QString &file : files)
        {
            const QFileInfo info(file);
            sub_options.append(modelEntryMap(info.completeBaseName(), file, {}));
        }
        entries.append(modelEntryMap(model.name, QString(), sub_options));
    }
    return entries;
}

} // namespace

QVariantList modelNestedOptions(const QString &project_dir, const QString &project_db,
                                const QString &framework_name, const QString &architecture,
                                const QStringList &extensions, const QStringList &variants,
                                const QString &variant_template, const QString &variant_param,
                                const QString &variant_value, const QVariant &current_value)
{
    QVariantList entries;

    const QString official_name = officialWeightName(variants, variant_template, variant_value);
    if (!official_name.isEmpty())
        entries.append(modelEntryMap(QStringLiteral("官方权重"), official_name, {}));

    entries += buildUserModelOptions(project_dir, project_db, framework_name, architecture, extensions,
                                     variant_param, variant_value);

    if (current_value.isValid() && !current_value.toString().trimmed().isEmpty())
    {
        const QString current = current_value.toString();
        bool          found   = false;
        for (const QVariant &entry_variant : entries)
        {
            const QVariantMap entry = entry_variant.toMap();
            if (entry.value(QStringLiteral("value")).toString() == current)
            {
                found = true;
                break;
            }
            const QVariantList sub_options = entry.value(QStringLiteral("subOptions")).toList();
            for (const QVariant &sub_variant : sub_options)
            {
                if (sub_variant.toMap().value(QStringLiteral("value")).toString() == current)
                {
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found)
        {
            QVariantList sub_options;
            sub_options.append(modelEntryMap(QStringLiteral("当前值 (%1)").arg(current), current, {}));
            entries.append(modelEntryMap(current, {}, sub_options));
        }
    }
    return entries;
}

} // namespace dltool::model


