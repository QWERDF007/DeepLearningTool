#include "model/ModelTestTaskRepository.h"

#include "common/Utils.h"
#include "database/DataBase.h"
#include "database/ModelDataBase.h"
#include "database/ModelTaskDataBase.h"
#include "model/ModelStorageService.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>

namespace dltool::model {

namespace {

bool setError(QString *err_msg, const QString &message)
{
    if (err_msg != nullptr)
        *err_msg = message;
    return false;
}

QList<dltool::database::DatasetSelectionRecord> testDatasetRecords(const ModelDatasetSelection &selection,
                                                                   const QString               &project_database_path)
{
    ModelDatasetSelections selections;
    selections.test           = selection;
    const auto class_resolver = [&project_database_path](const qint64 dataset_id) -> QList<qint64>
    {
        if (project_database_path.trimmed().isEmpty())
            return {};
        dltool::database::ProjectDataBase database(project_database_path);
        std::vector<int64_t>              class_ids;
        QString                           error;
        if (!database.labelClassIdsForDataset(dataset_id, class_ids, error))
            return {};
        QList<qint64> result;
        for (const int64_t class_id : class_ids) result.push_back(class_id);
        return result;
    };
    const QList<dltool::database::DatasetSelectionRecord> all = databaseDatasetSelections(selections, class_resolver);
    QList<dltool::database::DatasetSelectionRecord>       result;
    for (const auto &record : all)
    {
        if (record.type == QStringLiteral("test"))
            result.push_back(record);
    }
    return result;
}

bool sameName(const QString &lhs, const QString &rhs)
{
    return lhs.trimmed().compare(rhs.trimmed(), Qt::CaseInsensitive) == 0;
}

struct TestTaskJournal
{
    QString id;
    QString kind;   // "create", "rename", "remove"
    QString phase;  // "prepared", "staged", "directory-moved", "quarantined", "database-committed", "cleanup-pending"
    QString model_name;
    QString task_uuid;
    QString source_name;
    QString source_directory;
    QString target_name;
    QString target_directory;
    QString staging_path;
    QString quarantine_path;
};

QJsonObject toJson(const TestTaskJournal &journal)
{
    QJsonObject json;
    json[QStringLiteral("id")] = journal.id;
    json[QStringLiteral("kind")] = journal.kind;
    json[QStringLiteral("phase")] = journal.phase;
    json[QStringLiteral("model_name")] = journal.model_name;
    json[QStringLiteral("task_uuid")] = journal.task_uuid;
    if (!journal.source_name.isEmpty())
        json[QStringLiteral("source_name")] = journal.source_name;
    if (!journal.source_directory.isEmpty())
        json[QStringLiteral("source_directory")] = journal.source_directory;
    if (!journal.target_name.isEmpty())
        json[QStringLiteral("target_name")] = journal.target_name;
    if (!journal.target_directory.isEmpty())
        json[QStringLiteral("target_directory")] = journal.target_directory;
    if (!journal.staging_path.isEmpty())
        json[QStringLiteral("staging_path")] = journal.staging_path;
    if (!journal.quarantine_path.isEmpty())
        json[QStringLiteral("quarantine_path")] = journal.quarantine_path;
    return json;
}

bool fromJson(const QJsonObject &json, TestTaskJournal &journal)
{
    journal.id = json.value(QStringLiteral("id")).toString().trimmed();
    journal.kind = json.value(QStringLiteral("kind")).toString().trimmed();
    journal.phase = json.value(QStringLiteral("phase")).toString().trimmed();
    journal.model_name = json.value(QStringLiteral("model_name")).toString().trimmed();
    journal.task_uuid = json.value(QStringLiteral("task_uuid")).toString().trimmed();
    journal.source_name = json.value(QStringLiteral("source_name")).toString().trimmed();
    journal.source_directory = json.value(QStringLiteral("source_directory")).toString().trimmed();
    journal.target_name = json.value(QStringLiteral("target_name")).toString().trimmed();
    journal.target_directory = json.value(QStringLiteral("target_directory")).toString().trimmed();
    journal.staging_path = json.value(QStringLiteral("staging_path")).toString().trimmed();
    journal.quarantine_path = json.value(QStringLiteral("quarantine_path")).toString().trimmed();
    return !journal.id.isEmpty() && !journal.kind.isEmpty();
}

bool writeJournal(const QString &path, const TestTaskJournal &journal, QString *err_msg = nullptr)
{
    QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return setError(err_msg, QString("创建测试任务操作日志目录失败"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return setError(err_msg, QString("打开测试任务操作日志失败: %1").arg(file.errorString()));
    const QByteArray data = QJsonDocument(toJson(journal)).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size())
        return setError(err_msg, QString("写入测试任务操作日志失败: %1").arg(file.errorString()));
    file.flush();
    file.close();
    return true;
}

bool readJournal(const QString &path, TestTaskJournal &journal, QString *err_msg = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return setError(err_msg, QString("打开测试任务操作日志失败: %1").arg(file.errorString()));
    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject())
        return setError(err_msg, QString("解析测试任务操作日志失败: %1").arg(parse_error.errorString()));
    return fromJson(doc.object(), journal);
}

void removeJournal(const QString &path)
{
    QFile::remove(path);
}

QString readTaskIdFromDirectory(const QString &task_root)
{
    const QString db_path = QDir(task_root).filePath(QStringLiteral("task.db"));
    if (!QFileInfo::exists(db_path))
        return {};
    dltool::database::ModelTaskDataBase db(db_path);
    dltool::database::TaskInfoRecord info;
    QString error;
    if (db.readTaskInfo(info, &error))
        return info.task_id.trimmed();
    return {};
}

} // namespace

ModelTestTaskRepository::ModelTestTaskRepository(QString project_dir)
    : project_dir_(common::cleanPath(std::move(project_dir)))
{
}

void ModelTestTaskRepository::setProjectDirectory(const QString &project_dir)
{
    project_dir_ = common::cleanPath(project_dir);
}

void ModelTestTaskRepository::setProjectDatabasePath(const QString &project_database_path)
{
    project_database_path_ = common::cleanPath(project_database_path);
}

QString ModelTestTaskRepository::projectDirectory() const
{
    return project_dir_;
}

QString ModelTestTaskRepository::modelDatabasePath(const QString &model_name) const
{
    return ModelStorageService(project_dir_).modelDatabasePath(model_name);
}

QString ModelTestTaskRepository::validateTaskName(const QString &name)
{
    const QString value = name.trimmed();
    if (value.isEmpty())
        return QString("测试任务名称不能为空");
    if (value == QStringLiteral(".") || value == QStringLiteral(".."))
        return QString("测试任务名称无效");
    if (value.size() > 64)
        return QString("测试任务名称不能超过 64 个字符");
    if (value.endsWith(QChar(' ')) || value.endsWith(QChar('.')))
        return QString("测试任务名称不能以空格或点结尾");
    static const QRegularExpression invalid(QStringLiteral(R"([\\/:*?"<>|\x00-\x1f])"));
    if (invalid.match(value).hasMatch())
        return QString("测试任务名称包含非法路径字符");
    const QString                   stem = value.section(QChar('.'), 0, 0).toUpper();
    static const QRegularExpression reserved(QStringLiteral(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$)"));
    if (reserved.match(stem).hasMatch())
        return QString("测试任务名称不能使用 Windows 保留名称");
    return {};
}

QString ModelTestTaskRepository::directoryNameForTask(const QString &name)
{
    return name.trimmed();
}

QList<ModelTestTaskDefinition> ModelTestTaskRepository::listTasks(const QString &model_name, QString *err_msg) const
{
    QList<ModelTestTaskDefinition> result;
    const QString                  database_path = modelDatabasePath(model_name);
    if (database_path.isEmpty())
        return setError(err_msg, QString("模型数据库路径为空")), result;

    QString recovery_error;
    if (!recoverPending(model_name, &recovery_error))
    {
        spdlog::warn("模型 {} 存在未完成的测试任务恢复项: {}",
                     model_name.toUtf8().constData(), recovery_error.toUtf8().constData());
    }

    dltool::database::ModelDataBase              database(database_path);
    QList<dltool::database::ModelTestTaskRecord> records;
    if (!database.listTestTasks(records, err_msg))
        return result;

    QSet<QString> names;
    for (const auto &record : records)
    {
        if (validateTaskName(record.name).isEmpty() == false)
            return setError(err_msg, QString("模型数据库中的测试任务名称无效: %1").arg(record.name)),
                   QList<ModelTestTaskDefinition>{};
        const QString normalized_name = record.name.trimmed().toLower();
        if (names.contains(normalized_name))
            return setError(err_msg, QString("模型数据库中的测试任务名称重复: %1").arg(record.name)),
                   QList<ModelTestTaskDefinition>{};
        names.insert(normalized_name);

        ModelTestTaskDefinition task;
        task.uuid           = record.task_id;
        task.name           = record.name;
        task.directory_name = directoryNameForTask(record.name);
        task.created_at     = record.ctime;
        task.modified_at    = record.mtime;
        result.push_back(task);
    }
    return result;
}

bool ModelTestTaskRepository::ensureTaskRoot(const QString &model_name, const ModelTestTaskDefinition &task,
                                             QString *err_msg) const
{
    if (!ModelStorageService(project_dir_).ensureTestTaskStorage(model_name, task.directory_name, err_msg))
        return false;
    const QString database_path
        = ModelStorageService(project_dir_).testTaskDatabasePath(model_name, task.directory_name);
    if (database_path.isEmpty())
        return setError(err_msg, QString("测试任务数据库路径为空"));
    return true;
}

bool ModelTestTaskRepository::loadTask(const QString &model_name, const QString &uuid, ModelTestTaskDefinition &task,
                                       QString *err_msg) const
{
    const QList<ModelTestTaskDefinition> tasks = listTasks(model_name, err_msg);
    if (err_msg != nullptr && !err_msg->isEmpty())
        return false;
    const auto found = std::find_if(tasks.cbegin(), tasks.cend(),
                                    [&uuid](const auto &entry) { return entry.uuid == uuid.trimmed(); });
    if (found == tasks.cend())
        return setError(err_msg, QString("测试任务不存在: %1").arg(uuid));

    task = *found;
    const QString task_database_path
        = ModelStorageService(project_dir_).testTaskDatabasePath(model_name, task.directory_name);
    dltool::database::ModelTaskDataBase database(task_database_path);
    dltool::database::TaskInfoRecord    info;
    if (!database.readTaskInfo(info, err_msg))
        return false;
    if (info.task_id != task.uuid)
        return setError(err_msg, QString("task.db 与 model.db 的任务 ID 不一致"));
    task.created_at  = info.ctime;
    task.modified_at = info.mtime;

    if (!database.readTestParams(task.test_params, err_msg))
        return false;
    QList<dltool::database::DatasetSelectionRecord> dataset_records;
    if (!database.readDatasets(dataset_records, err_msg))
        return false;
    const ModelDatasetSelections selections = modelDatasetSelectionsFromDatabase(dataset_records);
    task.dataset_selection                  = selections.test;
    return true;
}

bool ModelTestTaskRepository::saveTask(const QString &model_name, const ModelTestTaskDefinition &task,
                                       const bool persist_selection, QString *err_msg) const
{
    ModelTestTaskDefinition value = task;
    value.name                    = value.name.trimmed();
    value.directory_name          = directoryNameForTask(value.name);
    if (!value.isValid())
        return setError(err_msg, QString("测试任务定义无效"));
    if (const QString name_error = validateTaskName(value.name); !name_error.isEmpty())
        return setError(err_msg, name_error);

    if (!ensureTaskRoot(model_name, value, err_msg))
        return false;

    const QString task_database_path
        = ModelStorageService(project_dir_).testTaskDatabasePath(model_name, value.directory_name);
    dltool::database::ModelTaskDataBase    task_database(task_database_path);
    const dltool::database::TaskInfoRecord info{value.uuid, value.created_at, value.modified_at};
    if (!task_database.upsertTaskInfo(info, err_msg) || !task_database.replaceTestParams(value.test_params, err_msg)
        || (persist_selection
            && !task_database.replaceDatasets(testDatasetRecords(value.dataset_selection, project_database_path_), err_msg)))
        return false;

    dltool::database::ModelDataBase             model_database(modelDatabasePath(model_name));
    const dltool::database::ModelTestTaskRecord index_record{value.uuid, value.name, value.created_at,
                                                             value.modified_at};
    return model_database.upsertTestTask(index_record, err_msg);
}

bool ModelTestTaskRepository::createTask(const QString &model_name, const QString &model_uuid, const QString &name,
                                         const QVariantMap &test_params, const ModelDatasetSelection &dataset_selection,
                                         ModelTestTaskDefinition &task, QString *err_msg) const
{
    const QString name_error = validateTaskName(name);
    if (!name_error.isEmpty())
        return setError(err_msg, name_error);

    QString recovery_error;
    if (!recoverPending(model_name, &recovery_error))
    {
        return setError(err_msg, QString("存在未恢复的测试任务操作: %1").arg(recovery_error));
    }

    const QList<ModelTestTaskDefinition> tasks = listTasks(model_name, err_msg);
    if (err_msg != nullptr && !err_msg->isEmpty())
        return false;
    for (const auto &existing : tasks)
    {
        if (sameName(existing.name, name))
            return setError(err_msg, QString("测试任务名称已存在: %1").arg(name));
    }

    ModelStorageService storage(project_dir_);
    const QString target_dir = directoryNameForTask(name);
    const QString target_root = storage.testTaskRoot(model_name, target_dir);
    if (QDir(target_root).exists())
        return setError(err_msg, QString("测试任务目标目录已存在"));

    const qint64 now       = QDateTime::currentSecsSinceEpoch();
    task                   = {};
    task.uuid              = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.model_uuid        = model_uuid.trimmed();
    task.name              = name.trimmed();
    task.directory_name    = target_dir;
    task.test_params       = test_params;
    task.dataset_selection = dataset_selection;
    task.created_at        = now;
    task.modified_at       = now;

    const QString op_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString journal_path = storage.testTaskOperationJournalPath(model_name, op_id);
    const QString staging_path = storage.testTaskOperationStagingRoot(model_name, op_id);

    TestTaskJournal journal;
    journal.id = op_id;
    journal.kind = QStringLiteral("create");
    journal.phase = QStringLiteral("prepared");
    journal.model_name = model_name;
    journal.task_uuid = task.uuid;
    journal.target_name = task.name;
    journal.target_directory = task.directory_name;
    journal.staging_path = staging_path;

    if (!writeJournal(journal_path, journal, err_msg))
        return false;

    if (!storage.ensureTestTaskStorageAt(staging_path, err_msg))
    {
        removeJournal(journal_path);
        return false;
    }

    {
        const QString staging_db_path = QDir(staging_path).filePath(QStringLiteral("task.db"));
        dltool::database::ModelTaskDataBase staging_db(staging_db_path);
        const dltool::database::TaskInfoRecord info{task.uuid, task.created_at, task.modified_at};
        if (!staging_db.upsertTaskInfo(info, err_msg)
            || !staging_db.replaceTestParams(task.test_params, err_msg)
            || !staging_db.replaceDatasets(testDatasetRecords(task.dataset_selection, project_database_path_), err_msg))
        {
            storage.removeDirectory(staging_path);
            removeJournal(journal_path);
            return false;
        }
    }

    journal.phase = QStringLiteral("staged");
    if (!writeJournal(journal_path, journal, err_msg))
    {
        storage.removeDirectory(staging_path);
        removeJournal(journal_path);
        return false;
    }

    dltool::database::ModelDataBase model_db(modelDatabasePath(model_name));
    const dltool::database::ModelTestTaskRecord index_record{task.uuid, task.name, task.created_at, task.modified_at};
    if (!model_db.upsertTestTask(index_record, err_msg))
    {
        storage.removeDirectory(staging_path);
        removeJournal(journal_path);
        return false;
    }

    journal.phase = QStringLiteral("database-committed");
    writeJournal(journal_path, journal);

    if (!storage.moveDirectory(staging_path, target_root, err_msg))
    {
        return false;
    }

    removeJournal(journal_path);
    return true;
}

bool ModelTestTaskRepository::renameTask(const QString &model_name, const QString &uuid, const QString &name,
                                         QString *err_msg) const
{
    const QString name_error = validateTaskName(name);
    if (!name_error.isEmpty())
        return setError(err_msg, name_error);

    QString recovery_error;
    if (!recoverPending(model_name, &recovery_error))
    {
        return setError(err_msg, QString("存在未恢复的测试任务操作: %1").arg(recovery_error));
    }

    QList<ModelTestTaskDefinition> tasks = listTasks(model_name, err_msg);
    if (err_msg != nullptr && !err_msg->isEmpty())
        return false;
    auto found
        = std::find_if(tasks.begin(), tasks.end(), [&uuid](const auto &entry) { return entry.uuid == uuid.trimmed(); });
    if (found == tasks.end())
        return setError(err_msg, QString("测试任务不存在: %1").arg(uuid));
    for (const auto &entry : tasks)
    {
        if (entry.uuid != uuid.trimmed() && sameName(entry.name, name))
            return setError(err_msg, QString("测试任务名称已存在: %1").arg(name));
    }

    const QString old_directory = found->directory_name;
    const QString new_directory = directoryNameForTask(name);
    if (old_directory == new_directory)
    {
        found->name        = name.trimmed();
        found->modified_at = QDateTime::currentSecsSinceEpoch();
        dltool::database::ModelDataBase database(modelDatabasePath(model_name));
        return database.upsertTestTask({found->uuid, found->name, found->created_at, found->modified_at}, err_msg);
    }

    ModelStorageService storage(project_dir_);
    const QString       old_root  = storage.testTaskRoot(model_name, old_directory);
    const QString       new_root  = storage.testTaskRoot(model_name, new_directory);
    if (old_root.isEmpty() || new_root.isEmpty() || QDir(new_root).exists())
        return setError(err_msg, QString("测试任务目标目录无效或已存在"));

    const QString op_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString journal_path = storage.testTaskOperationJournalPath(model_name, op_id);
    TestTaskJournal journal;
    journal.id = op_id;
    journal.kind = QStringLiteral("rename");
    journal.phase = QStringLiteral("prepared");
    journal.model_name = model_name;
    journal.task_uuid = found->uuid;
    journal.source_name = found->name;
    journal.source_directory = old_directory;
    journal.target_name = name.trimmed();
    journal.target_directory = new_directory;

    if (!writeJournal(journal_path, journal, err_msg))
        return false;

    if (!storage.moveDirectory(old_root, new_root, err_msg))
    {
        removeJournal(journal_path);
        return false;
    }

    journal.phase = QStringLiteral("directory-moved");
    writeJournal(journal_path, journal);

    found->name           = name.trimmed();
    found->directory_name = new_directory;
    found->modified_at    = QDateTime::currentSecsSinceEpoch();
    dltool::database::ModelDataBase database(modelDatabasePath(model_name));
    if (!database.upsertTestTask({found->uuid, found->name, found->created_at, found->modified_at}, err_msg))
    {
        if (!storage.moveDirectory(new_root, old_root))
            spdlog::error("重命名测试任务回滚失败: {}", old_root.toUtf8().constData());
        removeJournal(journal_path);
        return false;
    }

    journal.phase = QStringLiteral("database-committed");
    writeJournal(journal_path, journal);

    const QString task_database_path = storage.testTaskDatabasePath(model_name, new_directory);
    dltool::database::ModelTaskDataBase task_database(task_database_path);
    task_database.upsertTaskInfo({found->uuid, found->created_at, found->modified_at});

    removeJournal(journal_path);
    return true;
}

bool ModelTestTaskRepository::removeTask(const QString &model_name, const QString &uuid, QString *err_msg) const
{
    QString recovery_error;
    if (!recoverPending(model_name, &recovery_error))
    {
        return setError(err_msg, QString("存在未恢复的测试任务操作: %1").arg(recovery_error));
    }

    const QList<ModelTestTaskDefinition> tasks = listTasks(model_name, err_msg);
    if (err_msg != nullptr && !err_msg->isEmpty())
        return false;
    const auto found = std::find_if(tasks.cbegin(), tasks.cend(),
                                    [&uuid](const auto &entry) { return entry.uuid == uuid.trimmed(); });
    if (found == tasks.cend())
        return setError(err_msg, QString("测试任务不存在: %1").arg(uuid));

    ModelStorageService storage(project_dir_);
    const QString       root      = storage.testTaskRoot(model_name, found->directory_name);
    const QString       op_id     = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString       journal_path = storage.testTaskOperationJournalPath(model_name, op_id);
    const QString       quarantine_path = storage.testTaskOperationQuarantineRoot(model_name, op_id);

    TestTaskJournal journal;
    journal.id = op_id;
    journal.kind = QStringLiteral("remove");
    journal.phase = QStringLiteral("prepared");
    journal.model_name = model_name;
    journal.task_uuid = found->uuid;
    journal.source_name = found->name;
    journal.source_directory = found->directory_name;
    journal.quarantine_path = quarantine_path;

    if (!writeJournal(journal_path, journal, err_msg))
        return false;

    bool moved = false;
    if (!root.isEmpty() && QDir(root).exists())
    {
        if (!storage.moveDirectory(root, quarantine_path, err_msg))
        {
            removeJournal(journal_path);
            return false;
        }
        moved = true;
        journal.phase = QStringLiteral("quarantined");
        writeJournal(journal_path, journal);
    }

    dltool::database::ModelDataBase database(modelDatabasePath(model_name));
    if (!database.removeTestTask(found->uuid, err_msg))
    {
        if (moved)
            storage.moveDirectory(quarantine_path, root);
        removeJournal(journal_path);
        return false;
    }

    journal.phase = QStringLiteral("database-committed");
    writeJournal(journal_path, journal);

    if (moved && QDir(quarantine_path).exists())
    {
        QString remove_err;
        if (!storage.removeDirectory(quarantine_path, &remove_err))
        {
            journal.phase = QStringLiteral("cleanup-pending");
            writeJournal(journal_path, journal);
            spdlog::warn("删除测试任务隔离目录失败: {}", quarantine_path.toUtf8().constData());
            return setError(err_msg, QString("删除测试任务隔离目录失败: %1").arg(remove_err));
        }
    }

    removeJournal(journal_path);
    return true;
}

bool ModelTestTaskRepository::recoverPending(const QString &model_name, QString *err_msg) const
{
    ModelStorageService storage(project_dir_);
    const QString op_root = storage.testTaskOperationRoot(model_name);
    if (op_root.isEmpty() || !QDir(op_root).exists())
        return true;

    QDir dir(op_root);
    const QStringList journal_files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    if (journal_files.isEmpty())
        return true;

    const QString db_path = modelDatabasePath(model_name);
    dltool::database::ModelDataBase model_db(db_path);

    bool all_ok = true;
    for (const QString &filename : journal_files)
    {
        const QString journal_path = dir.filePath(filename);
        TestTaskJournal journal;
        QString read_error;
        if (!readJournal(journal_path, journal, &read_error))
        {
            spdlog::warn("读取测试任务操作日志失败: {}", read_error.toUtf8().constData());
            continue;
        }

        QList<dltool::database::ModelTestTaskRecord> db_records;
        QString db_error;
        if (!model_db.listTestTasks(db_records, &db_error))
        {
            if (err_msg != nullptr)
                *err_msg = db_error;
            all_ok = false;
            break;
        }

        auto db_it = std::find_if(db_records.begin(), db_records.end(),
                                  [&journal](const auto &rec) { return rec.task_id == journal.task_uuid; });
        const bool in_db = (db_it != db_records.end());

        if (journal.kind == QStringLiteral("create"))
        {
            const QString target_name = in_db ? db_it->name : journal.target_name;
            const QString target_dir = !journal.target_directory.isEmpty()
                ? journal.target_directory
                : directoryNameForTask(target_name);
            const QString target_root = storage.testTaskRoot(model_name, target_dir);

            if (!in_db)
            {
                if (!journal.staging_path.isEmpty() && QDir(journal.staging_path).exists())
                {
                    storage.removeDirectory(journal.staging_path);
                }
                removeJournal(journal_path);
            }
            else
            {
                if (QDir(target_root).exists())
                {
                    const QString stored_task_id = readTaskIdFromDirectory(target_root);
                    if (!stored_task_id.isEmpty() && stored_task_id != journal.task_uuid)
                    {
                        all_ok = false;
                        if (err_msg != nullptr)
                            *err_msg = QString("测试任务恢复目标目录冲突: %1 已存在且属于其他任务").arg(target_root);
                        continue;
                    }
                    if (!journal.staging_path.isEmpty() && QDir(journal.staging_path).exists())
                    {
                        storage.removeDirectory(journal.staging_path);
                    }
                    removeJournal(journal_path);
                }
                else
                {
                    if (!journal.staging_path.isEmpty() && QDir(journal.staging_path).exists())
                    {
                        QString move_err;
                        if (!storage.moveDirectory(journal.staging_path, target_root, &move_err))
                        {
                            all_ok = false;
                            if (err_msg != nullptr)
                                *err_msg = move_err;
                            continue;
                        }
                    }
                    else
                    {
                        storage.ensureTestTaskStorage(model_name, target_dir);
                    }
                    removeJournal(journal_path);
                }
            }
        }
        else if (journal.kind == QStringLiteral("rename"))
        {
            if (!in_db)
            {
                removeJournal(journal_path);
            }
            else
            {
                const QString current_db_name = db_it->name.trimmed();
                const QString target_name = journal.target_name.trimmed();
                const QString source_name = journal.source_name.trimmed();

                const QString expected_dir = directoryNameForTask(current_db_name);
                const QString expected_root = storage.testTaskRoot(model_name, expected_dir);

                const QString other_dir = sameName(current_db_name, target_name)
                    ? (!journal.source_directory.isEmpty() ? journal.source_directory : directoryNameForTask(source_name))
                    : (!journal.target_directory.isEmpty() ? journal.target_directory : directoryNameForTask(target_name));
                const QString other_root = storage.testTaskRoot(model_name, other_dir);

                if (QDir(expected_root).exists())
                {
                    const QString stored_task_id = readTaskIdFromDirectory(expected_root);
                    if (stored_task_id == journal.task_uuid)
                    {
                        if (QDir(other_root).exists())
                        {
                            if (readTaskIdFromDirectory(other_root) == journal.task_uuid)
                                storage.removeDirectory(other_root);
                        }
                        removeJournal(journal_path);
                    }
                    else
                    {
                        all_ok = false;
                        if (err_msg != nullptr)
                            *err_msg = QString("恢复重命名测试任务遇到目标目录冲突: %1 已存在且属于其他任务").arg(expected_root);
                        continue;
                    }
                }
                else
                {
                    if (QDir(other_root).exists())
                    {
                        const QString stored_id = readTaskIdFromDirectory(other_root);
                        if (!stored_id.isEmpty() && stored_id != journal.task_uuid)
                        {
                            all_ok = false;
                            if (err_msg != nullptr)
                                *err_msg = QString("恢复重命名测试任务源目录冲突: %1 已存在且属于其他任务").arg(other_root);
                            continue;
                        }
                        QString move_err;
                        if (!storage.moveDirectory(other_root, expected_root, &move_err))
                        {
                            all_ok = false;
                            if (err_msg != nullptr)
                                *err_msg = move_err;
                            continue;
                        }
                        removeJournal(journal_path);
                    }
                    else
                    {
                        storage.ensureTestTaskStorage(model_name, expected_dir);
                        removeJournal(journal_path);
                    }
                }
            }
        }
        else if (journal.kind == QStringLiteral("remove"))
        {
            if (in_db)
            {
                const QString task_dir = !journal.source_directory.isEmpty()
                    ? journal.source_directory
                    : directoryNameForTask(db_it->name);
                const QString task_root = storage.testTaskRoot(model_name, task_dir);

                if (QDir(task_root).exists())
                {
                    const QString stored_id = readTaskIdFromDirectory(task_root);
                    if (!stored_id.isEmpty() && stored_id != journal.task_uuid)
                    {
                        all_ok = false;
                        if (err_msg != nullptr)
                            *err_msg = QString("恢复删除测试任务遇到目标目录冲突: %1 已存在且属于其他任务").arg(task_root);
                        continue;
                    }
                    if (!journal.quarantine_path.isEmpty() && QDir(journal.quarantine_path).exists())
                    {
                        storage.removeDirectory(journal.quarantine_path);
                    }
                    removeJournal(journal_path);
                }
                else
                {
                    if (!journal.quarantine_path.isEmpty() && QDir(journal.quarantine_path).exists())
                    {
                        QString move_err;
                        if (!storage.moveDirectory(journal.quarantine_path, task_root, &move_err))
                        {
                            all_ok = false;
                            if (err_msg != nullptr)
                                *err_msg = move_err;
                            continue;
                        }
                    }
                    else
                    {
                        storage.ensureTestTaskStorage(model_name, task_dir);
                    }
                    removeJournal(journal_path);
                }
            }
            else
            {
                if (!journal.quarantine_path.isEmpty() && QDir(journal.quarantine_path).exists())
                {
                    QString remove_err;
                    if (!storage.removeDirectory(journal.quarantine_path, &remove_err))
                    {
                        journal.phase = QStringLiteral("cleanup-pending");
                        writeJournal(journal_path, journal);
                        all_ok = false;
                        if (err_msg != nullptr)
                            *err_msg = QString("清理隔离目录失败: %1").arg(remove_err);
                        continue;
                    }
                }
                removeJournal(journal_path);
            }
        }
    }

    return all_ok;
}

} // namespace dltool::model
