#include "model/ModelTestTaskRepository.h"

#include "common/Utils.h"
#include "database/DataBase.h"
#include "database/ModelDataBase.h"
#include "database/ModelTaskDataBase.h"
#include "model/ModelStorageService.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
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
                                       QString *err_msg) const
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
        || !task_database.replaceDatasets(testDatasetRecords(value.dataset_selection, project_database_path_), err_msg))
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

    const QList<ModelTestTaskDefinition> tasks = listTasks(model_name, err_msg);
    if (err_msg != nullptr && !err_msg->isEmpty())
        return false;
    for (const auto &existing : tasks)
    {
        if (sameName(existing.name, name))
            return setError(err_msg, QString("测试任务名称已存在: %1").arg(name));
    }

    const qint64 now       = QDateTime::currentSecsSinceEpoch();
    task                   = {};
    task.uuid              = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.model_uuid        = model_uuid.trimmed();
    task.name              = name.trimmed();
    task.directory_name    = directoryNameForTask(task.name);
    task.test_params       = test_params;
    task.dataset_selection = dataset_selection;
    task.created_at        = now;
    task.modified_at       = now;
    return saveTask(model_name, task, err_msg);
}

bool ModelTestTaskRepository::renameTask(const QString &model_name, const QString &uuid, const QString &name,
                                         QString *err_msg) const
{
    const QString name_error = validateTaskName(name);
    if (!name_error.isEmpty())
        return setError(err_msg, name_error);
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
    const QString       test_root = storage.testRoot(model_name);
    const QString       old_root  = storage.testTaskRoot(model_name, old_directory);
    const QString       new_root  = storage.testTaskRoot(model_name, new_directory);
    if (old_root.isEmpty() || new_root.isEmpty() || QDir(new_root).exists())
        return setError(err_msg, QString("测试任务目标目录无效或已存在"));
    QDir parent(test_root);
    if (!parent.rename(old_directory, new_directory))
        return setError(err_msg, QString("重命名测试任务目录失败"));

    found->name           = name.trimmed();
    found->directory_name = new_directory;
    found->modified_at    = QDateTime::currentSecsSinceEpoch();
    dltool::database::ModelDataBase database(modelDatabasePath(model_name));
    if (!database.upsertTestTask({found->uuid, found->name, found->created_at, found->modified_at}, err_msg))
    {
        if (!parent.rename(new_directory, old_directory))
            spdlog::error("重命名测试任务回滚失败: {}", old_root.toUtf8().constData());
        return false;
    }
    return true;
}

bool ModelTestTaskRepository::removeTask(const QString &model_name, const QString &uuid, QString *err_msg) const
{
    const QList<ModelTestTaskDefinition> tasks = listTasks(model_name, err_msg);
    if (err_msg != nullptr && !err_msg->isEmpty())
        return false;
    const auto found = std::find_if(tasks.cbegin(), tasks.cend(),
                                    [&uuid](const auto &entry) { return entry.uuid == uuid.trimmed(); });
    if (found == tasks.cend())
        return setError(err_msg, QString("测试任务不存在: %1").arg(uuid));

    ModelStorageService storage(project_dir_);
    const QString       root      = storage.testTaskRoot(model_name, found->directory_name);
    const QString       test_root = storage.testRoot(model_name);
    QString             temporary_name;
    bool                moved = false;
    if (!root.isEmpty() && QDir(root).exists())
    {
        temporary_name
            = found->directory_name + QStringLiteral(".delete-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir parent(test_root);
        if (!parent.rename(found->directory_name, temporary_name))
            return setError(err_msg, QString("移动待删除测试任务目录失败"));
        moved = true;
    }

    dltool::database::ModelDataBase database(modelDatabasePath(model_name));
    if (!database.removeTestTask(found->uuid, err_msg))
    {
        if (moved)
            QDir(test_root).rename(temporary_name, found->directory_name);
        return false;
    }
    if (moved && !QDir(test_root).rmdir(temporary_name))
    {
        // removeRecursively is intentionally used only on the validated,
        // private temporary task directory.
        if (!QDir(QDir(test_root).filePath(temporary_name)).removeRecursively())
            spdlog::warn("删除测试任务临时目录失败: {}", temporary_name.toUtf8().constData());
    }
    return true;
}

} // namespace dltool::model
