#include "model/ModelStorageMigration.h"

#include "common/YamlUtils.h"
#include "model/ModelStorageService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QSaveFile>
#include <QVariantList>
#include <QUuid>
#include <yaml-cpp/yaml.h>

#include <exception>

namespace dltool::model {

namespace {

QString yamlKey(const char *value)
{
    return QString::fromLatin1(value);
}

bool setError(ModelStorageMigrationResult &result, const QString &message)
{
    result.error = message;
    return false;
}

bool restoreFile(const QString &path, const QByteArray &contents)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(contents) != contents.size())
        return false;
    return file.commit();
}

bool movePath(const QString &source, const QString &target, ModelStorageMigrationResult &result,
              QList<QPair<QString, QString>> *moved_paths = nullptr)
{
    const QFileInfo source_info(source);
    if (!source_info.exists())
        return true;

    const QFileInfo target_info(target);
    if (target_info.exists())
    {
        // ensureModelStorage() may already have created empty destination
        // directories.  Reuse those, but never delete non-empty user data.
        if (source_info.isDir() && target_info.isDir() && QDir(target).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
        {
            if (!QDir().rmdir(target))
                return setError(result, QString("无法替换空迁移目录: %1").arg(target));
        }
        else
        {
            // A non-empty destination (or an existing destination file) is
            // ambiguous.  Silently keeping the legacy source would leave two
            // competing configurations while reporting a successful migration.
            return setError(result, QString("迁移目标已存在且不为空: %1").arg(target));
        }
    }

    if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::rename(source, target))
        return setError(result, QString("迁移路径失败: %1 -> %2").arg(source, target));
    if (moved_paths != nullptr)
        moved_paths->push_back({source, target});
    result.migrated = true;
    return true;
}

void rollbackMoves(const QList<QPair<QString, QString>> &moved_paths)
{
    for (auto it = moved_paths.crbegin(); it != moved_paths.crend(); ++it)
    {
        const QString &source = it->first;
        const QString &target = it->second;
        if (!QFileInfo::exists(target))
            continue;
        const QFileInfo source_info(source);
        if (source_info.exists())
        {
            if (source_info.isDir())
                QDir(source).removeRecursively();
            else
                QFile::remove(source);
        }
        QDir().mkpath(QFileInfo(source).absolutePath());
        QFile::rename(target, source);
    }
}

QVariantMap normalizeLegacySelection(const QVariantMap &raw)
{
    QVariantList dataset_ids;
    QVariantList label_classes;

    const auto appendId = [](QVariantList &target, const QVariant &value)
    {
        bool ok = false;
        const qint64 id = value.toLongLong(&ok);
        if (ok && id >= 0 && !target.contains(id))
            target.push_back(id);
    };
    const auto appendClass = [&appendId](QVariantList &target, const QVariant &value)
    {
        const QVariantMap entry = value.toMap();
        bool dataset_ok = false;
        bool class_ok = false;
        const qint64 dataset_id = entry.value(yamlKey("dataset_id")).toLongLong(&dataset_ok);
        const qint64 class_id = entry.value(yamlKey("label_class_id"),
                                             entry.value(yamlKey("class_id"))).toLongLong(&class_ok);
        if (dataset_ok && class_ok && dataset_id >= 0 && class_id >= 0)
            target.push_back(QVariantMap{{yamlKey("dataset_id"), dataset_id},
                                         {yamlKey("label_class_id"), class_id}});
    };

    const QVariantList raw_dataset_ids = raw.value(yamlKey("dataset_ids"),
                                                    raw.value(yamlKey("selected_dataset_ids"))).toList();
    for (const QVariant &value : raw_dataset_ids)
        appendId(dataset_ids, value);
    const QVariant value_dataset = raw.value(yamlKey("dataset_id"));
    if (value_dataset.isValid())
        appendId(dataset_ids, value_dataset);

    const QVariantList raw_classes = raw.value(yamlKey("label_classes"),
                                                raw.value(yamlKey("selected_label_classes"))).toList();
    for (const QVariant &value : raw_classes)
        appendClass(label_classes, value);

    // A few pre-v2 configurations stored selected datasets as a list of
    // maps.  Accept only explicit IDs; generated manifest entries are not a
    // selection and must not be guessed into the task definition.
    const QVariantList legacy_datasets = raw.value(yamlKey("datasets")).toList();
    for (const QVariant &value : legacy_datasets)
    {
        const QVariantMap entry = value.toMap();
        if (!entry.isEmpty())
            appendId(dataset_ids, entry.value(yamlKey("id"), entry.value(yamlKey("dataset_id"))));
        else
            appendId(dataset_ids, value);
    }

    return {{yamlKey("dataset_ids"), dataset_ids}, {yamlKey("label_classes"), label_classes}};
}

bool writeMetadata(const QString &path, const QString &status, const QString &error,
                   ModelStorageMigrationResult &result)
{
    QVariantMap metadata{{yamlKey("schema_version"), 2},
                         {yamlKey("migration_status"), status},
                         {yamlKey("migrated_at"), QDateTime::currentSecsSinceEpoch()}};
    if (!error.isEmpty())
        metadata.insert(yamlKey("migration_error"), error);
    QString write_error;
    if (!common::yaml::writeFileAtomic(path, common::yaml::variantToYaml(metadata), &write_error,
                                       QString("打开 storage.yaml 失败"), QString("生成 storage.yaml 失败"),
                                       QString("提交 storage.yaml 失败")))
        return setError(result, write_error);
    return true;
}

QVariantMap loadLegacyMap(const QString &path, ModelStorageMigrationResult &result)
{
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile())
        return {};
    try
    {
        const YAML::Node root = common::yaml::loadFile(file);
        if (!root || !root.IsMap())
        {
            setError(result, QString("旧模型配置不是 YAML map: %1").arg(path));
            return {};
        }
        return common::yaml::nodeVariant(root).toMap();
    }
    catch (const std::exception &exception)
    {
        setError(result, QString("读取旧模型配置失败: %1").arg(QString(exception.what())));
        return {};
    }
}

bool createDefaultTask(const ModelStorageService &storage, const QString &model_name, const QString &model_uuid,
                       const QVariantMap &legacy_config, ModelStorageMigrationResult &result,
                       QString &task_directory)
{
    const QString tasks_path = storage.testTasksPath(model_name);
    if (QFileInfo::exists(tasks_path))
    {
        try
        {
            const YAML::Node index = common::yaml::loadFile(QFileInfo(tasks_path));
            const QString current_uuid = common::yaml::nodeString(index[yamlKey("current_task_uuid").toStdString()]);
            const YAML::Node entries = index[yamlKey("tasks").toStdString()];
            if (!entries || !entries.IsSequence() || entries.size() == 0)
                return setError(result, QString("现有测试任务索引为空或格式无效"));
            for (const YAML::Node &entry : entries)
            {
                if (!entry || !entry.IsMap())
                    continue;
                const QString uuid = common::yaml::nodeString(entry[yamlKey("uuid").toStdString()]);
                if (uuid == current_uuid || (current_uuid.isEmpty() && task_directory.isEmpty()))
                {
                    task_directory = common::yaml::nodeString(entry[yamlKey("directory_name").toStdString()]);
                    break;
                }
            }
            if (task_directory.isEmpty())
                task_directory = common::yaml::nodeString(entries[0][yamlKey("directory_name").toStdString()]);
            if (task_directory.isEmpty())
                return setError(result, QString("现有测试任务目录为空"));
            return true;
        }
        catch (const std::exception &exception)
        {
            return setError(result, QString("读取现有测试任务索引失败: %1").arg(QString(exception.what())));
        }
    }

    const QString task_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task_directory = QString("默认测试");
    const QString task_root = storage.testTaskRoot(model_name, task_directory);
    if (task_root.isEmpty() || !QDir().mkpath(task_root))
        return setError(result, QString("创建默认测试任务目录失败"));

    QVariantMap test_params = legacy_config.value(yamlKey("test_params")).toMap();
    if (test_params.isEmpty())
    {
        // Some old configs stored inference options at the top level.
        const QVariantMap inference = legacy_config.value(yamlKey("inference")).toMap();
        if (!inference.isEmpty())
            test_params.insert(yamlKey("inference"), inference);
    }
    QVariantMap selection = legacy_config.value(yamlKey("dataset_selection")).toMap();
    if (selection.isEmpty())
        selection = legacy_config.value(yamlKey("datasets")).toMap().value(yamlKey("test")).toMap();
    selection = normalizeLegacySelection(selection);

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QVariantMap task_config{{yamlKey("uuid"), task_uuid},
                                  {yamlKey("model_uuid"), model_uuid},
                                  {yamlKey("name"), task_directory},
                                  {yamlKey("directory_name"), task_directory},
                                  {yamlKey("created_at"), now},
                                  {yamlKey("modified_at"), now},
                                  {yamlKey("test_params"), test_params},
                                  {yamlKey("dataset_selection"), selection}};
    QString write_error;
    if (!common::yaml::writeFileAtomic(storage.testTaskConfigPath(model_name, task_directory),
                                       common::yaml::variantToYaml(task_config), &write_error,
                                       QString("打开默认测试配置失败"), QString("生成默认测试配置失败"),
                                       QString("提交默认测试配置失败")))
        return setError(result, write_error);

    const QVariantMap index{{yamlKey("schema_version"), 1},
                            {yamlKey("current_task_uuid"), task_uuid},
                            {yamlKey("tasks"), QVariantList{task_config}}};
    if (!common::yaml::writeFileAtomic(tasks_path, common::yaml::variantToYaml(index), &write_error,
                                       QString("打开测试任务索引失败"), QString("生成测试任务索引失败"),
                                       QString("提交测试任务索引失败")))
    {
        QDir(task_root).removeRecursively();
        return setError(result, write_error);
    }
    result.legacy_test_created = true;
    result.migrated = true;
    return true;
}

} // namespace

ModelStorageMigrationResult migrateModelStorage(const QString &project_dir, const QString &model_name,
                                                const QString &model_uuid)
{
    ModelStorageMigrationResult result;
    const ModelStorageService storage(project_dir);
    const QString model_root = storage.path(model_name, ModelStorageLocation::ModelRoot);
    const QString metadata_path = storage.storageMetadataPath(model_name);
    if (model_root.isEmpty() || metadata_path.isEmpty())
    {
        result.error = QString("模型存储目录为空");
        return result;
    }
    if (!QDir().mkpath(model_root))
    {
        result.error = QString("创建模型存储目录失败: %1").arg(model_root);
        return result;
    }

    const QFileInfo metadata_file(metadata_path);
    if (metadata_file.exists())
    {
        try
        {
            const YAML::Node metadata = common::yaml::loadFile(metadata_file);
            if (metadata && metadata.IsMap() && metadata[yamlKey("schema_version").toStdString()]
                && metadata[yamlKey("schema_version").toStdString()].as<int>() >= 2
                && common::yaml::nodeString(metadata[yamlKey("migration_status").toStdString()])
                       == QString("complete"))
                return result;
        }
        catch (const std::exception &)
        {
            // 损坏的元数据不能阻止下一次幂等迁移，后续会用完整版本覆盖它。
        }
    }

    QList<QPair<QString, QString>> moved_paths;
    bool created_default_task = false;
    bool removed_legacy_test_config = false;
    QByteArray legacy_test_config_bytes;
    QString task_directory;
    const QString config_root = storage.path(model_name, ModelStorageLocation::Configs);
    const QString legacy_train_config = QDir::cleanPath(QDir(config_root).filePath(QStringLiteral("train.yaml")));
    const QString legacy_test_config = QDir::cleanPath(QDir(config_root).filePath(QStringLiteral("test.yaml")));
    const QVariantMap legacy_test = loadLegacyMap(legacy_test_config, result);
    if (!result.error.isEmpty())
    {
        writeMetadata(metadata_path, QString("failed"), result.error, result);
        return result;
    }

    auto abortMigration = [&]()
    {
        rollbackMoves(moved_paths);
        if (removed_legacy_test_config && !QFileInfo::exists(legacy_test_config))
            restoreFile(legacy_test_config, legacy_test_config_bytes);
        if (created_default_task)
        {
            QDir(storage.testTaskRoot(model_name, task_directory)).removeRecursively();
            QFile::remove(storage.testTasksPath(model_name));
        }
        writeMetadata(metadata_path, QString("failed"), result.error, result);
        return result;
    };

    if (!movePath(legacy_train_config, storage.trainConfigPath(model_name), result, &moved_paths)
        || !movePath(storage.path(model_name, ModelStorageLocation::Weights), storage.trainWeightsPath(model_name), result,
                     &moved_paths)
        || !movePath(QDir(config_root).filePath(QStringLiteral("../datasets/datasets.yaml")),
                     QDir(storage.trainDatasetPath(model_name)).filePath(QStringLiteral("datasets.yaml")), result,
                     &moved_paths))
    {
        return abortMigration();
    }

    const bool has_legacy_test = QFileInfo::exists(legacy_test_config)
        || QFileInfo::exists(storage.path(model_name, ModelStorageLocation::Results) + QStringLiteral("/pred"));
    if (has_legacy_test)
    {
        const bool had_tasks = QFileInfo::exists(storage.testTasksPath(model_name));
        created_default_task = !had_tasks;
        if (!createDefaultTask(storage, model_name, model_uuid, legacy_test, result, task_directory))
            return abortMigration();
        if (!movePath(storage.path(model_name, ModelStorageLocation::Results) + QStringLiteral("/pred"),
                      storage.testTaskPredictionPath(model_name, task_directory), result, &moved_paths))
            return abortMigration();
        const QString migrated_task_config = storage.testTaskConfigPath(model_name, task_directory);
        // createDefaultTask() materializes the normalized task config.  Do
        // not treat that intentional replacement as a destination conflict;
        // remove the legacy source only after the normalized file exists.
        if (QFileInfo::exists(legacy_test_config))
        {
            if (QFileInfo::exists(migrated_task_config))
            {
                QFile old_config(legacy_test_config);
                if (!old_config.open(QIODevice::ReadOnly))
                {
                    setError(result, QString("读取旧测试配置失败: %1").arg(old_config.errorString()));
                    return abortMigration();
                }
                legacy_test_config_bytes = old_config.readAll();
                old_config.close();
                if (!QFile::remove(legacy_test_config))
                {
                    setError(result, QString("删除旧测试配置失败: %1").arg(legacy_test_config));
                    return abortMigration();
                }
                removed_legacy_test_config = true;
                result.migrated = true;
            }
            else if (!movePath(legacy_test_config, migrated_task_config, result, &moved_paths))
                return abortMigration();
        }
    }

    const QString legacy_logs = storage.path(model_name, ModelStorageLocation::Logs);
    const QString train_legacy_log = QDir(legacy_logs).filePath(QStringLiteral("train.log"));
    if (!movePath(train_legacy_log, QDir(storage.trainLogsPath(model_name)).filePath(QStringLiteral("train-legacy.log")), result,
                  &moved_paths))
        return abortMigration();
    if (has_legacy_test)
    {
        const QString test_legacy_log = QDir(legacy_logs).filePath(QStringLiteral("test.log"));
        QString task_uuid;
        try
        {
            const YAML::Node index = common::yaml::loadFile(QFileInfo(storage.testTasksPath(model_name)));
            task_uuid = common::yaml::nodeString(index[yamlKey("current_task_uuid").toStdString()]);
        }
        catch (const std::exception &)
        {
        }
        if (task_uuid.isEmpty())
            task_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!movePath(test_legacy_log, storage.testTaskLogPath(model_name, task_uuid), result, &moved_paths))
            return abortMigration();
    }

    if (!writeMetadata(metadata_path, QString("complete"), {}, result))
        return abortMigration();
    return result;
}

} // namespace dltool::model
