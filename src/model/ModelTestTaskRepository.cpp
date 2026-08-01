#include "model/ModelTestTaskRepository.h"

#include "common/Utils.h"
#include "common/YamlUtils.h"
#include "model/ModelStorageService.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <exception>

namespace dltool::model {

namespace {

constexpr qint64 kMaxTaskIndexBytes = 16LL * 1024LL * 1024LL;
constexpr std::size_t kMaxTaskRecords = 10'000;

QString key(const char *value)
{
    return QString::fromLatin1(value);
}

QVariantMap selectionToMap(const ModelDatasetSelection &selection)
{
    QVariantList dataset_ids;
    for (const qint64 id : selection.dataset_ids)
        dataset_ids.push_back(id);

    QVariantList label_classes;
    for (const auto &[dataset_id, label_class_id] : selection.label_classes)
    {
        label_classes.push_back(QVariantMap{{key("dataset_id"), dataset_id},
                                            {key("label_class_id"), label_class_id}});
    }
    return {{key("dataset_ids"), dataset_ids}, {key("label_classes"), label_classes}};
}

ModelDatasetSelection selectionFromMap(const QVariantMap &map)
{
    ModelDatasetSelection selection;
    for (const QVariant &value : map.value(key("dataset_ids")).toList())
    {
        bool ok = false;
        const qint64 id = value.toLongLong(&ok);
        if (ok && id >= 0)
            selection.dataset_ids.insert(id);
    }
    for (const QVariant &value : map.value(key("label_classes")).toList())
    {
        const QVariantMap entry = value.toMap();
        bool dataset_ok = false;
        bool class_ok = false;
        const qint64 dataset_id = entry.value(key("dataset_id")).toLongLong(&dataset_ok);
        const qint64 label_class_id = entry.value(key("label_class_id")).toLongLong(&class_ok);
        if (dataset_ok && class_ok && dataset_id >= 0 && label_class_id >= 0)
            selection.label_classes.insert({dataset_id, label_class_id});
    }
    return selection;
}

QVariantMap taskToMap(const ModelTestTaskDefinition &task)
{
    return {{key("uuid"), task.uuid},
            {key("model_uuid"), task.model_uuid},
            {key("name"), task.name},
            {key("directory_name"), task.directory_name},
            {key("created_at"), task.created_at},
            {key("modified_at"), task.modified_at},
            {key("test_params"), task.test_params},
            {key("dataset_selection"), selectionToMap(task.dataset_selection)}};
}

ModelTestTaskDefinition taskFromMap(const QVariantMap &map)
{
    ModelTestTaskDefinition task;
    task.uuid = map.value(key("uuid")).toString().trimmed();
    task.model_uuid = map.value(key("model_uuid")).toString().trimmed();
    task.name = map.value(key("name")).toString();
    task.directory_name = map.value(key("directory_name")).toString();
    task.created_at = map.value(key("created_at")).toLongLong();
    task.modified_at = map.value(key("modified_at")).toLongLong();
    task.test_params = map.value(key("test_params")).toMap();
    task.dataset_selection = selectionFromMap(map.value(key("dataset_selection")).toMap());
    return task;
}

bool sameName(const QString &lhs, const QString &rhs)
{
    return lhs.trimmed().compare(rhs.trimmed(), Qt::CaseInsensitive) == 0;
}

bool setError(QString *err_msg, const QString &message)
{
    if (err_msg != nullptr)
        *err_msg = message;
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

bool validIndexSchema(const YAML::Node &root, QString *err_msg)
{
    if (!root || !root.IsMap())
        return setError(err_msg, QString("测试任务索引不是 YAML map"));
    const YAML::Node schema = root[key("schema_version").toStdString()];
    if (!schema || !schema.IsScalar() || schema.as<int>() != 1)
        return setError(err_msg, QString("测试任务索引 schema_version 无效"));
    const YAML::Node tasks = root[key("tasks").toStdString()];
    if (!tasks || !tasks.IsSequence())
        return setError(err_msg, QString("测试任务索引缺少 tasks sequence"));
    if (tasks.size() > kMaxTaskRecords)
        return setError(err_msg, QString("测试任务索引 tasks 数量超过限制"));
    return true;
}

bool validTaskDefinition(const ModelTestTaskDefinition &task, QString *err_msg)
{
    if (!task.isValid())
        return setError(err_msg, QString("测试任务定义字段不完整"));
    if (ModelTestTaskRepository::validateTaskName(task.name).isEmpty() == false)
        return setError(err_msg, ModelTestTaskRepository::validateTaskName(task.name));
    if (task.directory_name != ModelTestTaskRepository::directoryNameForTask(task.name))
        return setError(err_msg, QString("测试任务目录名与名称不一致"));
    return true;
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

QString ModelTestTaskRepository::projectDirectory() const
{
    return project_dir_;
}

QString ModelTestTaskRepository::tasksPath(const QString &model_name) const
{
    return ModelStorageService(project_dir_).testTasksPath(model_name);
}

QString ModelTestTaskRepository::currentTaskUuid(const QString &model_name, QString *err_msg) const
{
    const QString path = tasksPath(model_name);
    if (path.isEmpty())
    {
        if (err_msg)
            *err_msg = QString("测试任务索引路径为空");
        return {};
    }
    const QFileInfo file(path);
    if (!file.exists())
        return {};
    if (file.size() > kMaxTaskIndexBytes)
    {
        if (err_msg)
            *err_msg = QString("测试任务索引超过大小限制");
        return {};
    }
    try
    {
        const YAML::Node root = common::yaml::loadFile(file);
        if (!validIndexSchema(root, err_msg))
            return {};
        return common::yaml::nodeString(root[key("current_task_uuid").toStdString()]);
    }
    catch (const std::exception &e)
    {
        if (err_msg)
            *err_msg = QString("读取当前测试任务失败: %1").arg(QString(e.what()));
        return {};
    }
}

bool ModelTestTaskRepository::setCurrentTaskUuid(const QString &model_name, const QString &uuid,
                                                 QString *err_msg) const
{
    const QString path = tasksPath(model_name);
    if (path.isEmpty())
        return setError(err_msg, QString("测试任务索引路径为空"));
    QVariantMap root{{key("schema_version"), 1}};
    const QFileInfo file(path);
    if (file.exists())
    {
        if (file.size() > kMaxTaskIndexBytes)
            return setError(err_msg, QString("测试任务索引超过大小限制"));
        try
        {
            const YAML::Node node = common::yaml::loadFile(file);
            if (!validIndexSchema(node, err_msg))
                return false;
            root = common::yaml::nodeVariant(node).toMap();
        }
        catch (const std::exception &e)
        {
            return setError(err_msg, QString("读取测试任务索引失败: %1").arg(QString(e.what())));
        }
    }
    QString list_error;
    const QList<ModelTestTaskDefinition> tasks = listTasks(model_name, &list_error);
    if (!list_error.isEmpty())
        return setError(err_msg, list_error);
    const QString selected = uuid.trimmed();
    if (!selected.isEmpty())
    {
        const auto found = std::find_if(tasks.cbegin(), tasks.cend(), [&selected](const auto &task)
                                        { return task.uuid == selected; });
        if (found == tasks.cend())
            return setError(err_msg, QString("当前测试任务不存在: %1").arg(selected));
    }
    root.insert(key("current_task_uuid"), selected);
    return common::yaml::writeFileAtomic(path, common::yaml::variantToYaml(root), err_msg,
                                         QString("打开测试任务索引失败"), QString("生成测试任务索引失败"),
                                         QString("提交测试任务索引失败"));
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
    // Windows reserves these device names even when an extension is added;
    // rejecting the stem keeps the task directory portable across platforms.
    const QString stem = value.section(QChar('.'), 0, 0).toUpper();
    static const QRegularExpression reserved(
        QStringLiteral(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$)"));
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
    const QString path = tasksPath(model_name);
    if (path.isEmpty())
        return setError(err_msg, QString("测试任务索引路径为空")), result;

    const QFileInfo file(path);
    if (!file.exists())
        return result;
    if (file.size() > kMaxTaskIndexBytes)
        return setError(err_msg, QString("测试任务索引超过大小限制")), result;
    try
    {
        const YAML::Node root = common::yaml::loadFile(file);
        if (!validIndexSchema(root, err_msg))
            return result;
        const YAML::Node entries = root[key("tasks").toStdString()];
        QSet<QString> uuids;
        QSet<QString> names;
        for (const YAML::Node &entry : entries)
        {
            if (!entry || !entry.IsMap())
                return setError(err_msg, QString("测试任务索引项不是 YAML map")), QList<ModelTestTaskDefinition>{};
            const ModelTestTaskDefinition task = taskFromMap(common::yaml::nodeVariant(entry).toMap());
            if (!validTaskDefinition(task, err_msg))
                return {};
            const QString uuid = task.uuid;
            const QString name = task.name.toLower();
            if (uuids.contains(uuid) || names.contains(name))
                return setError(err_msg, QString("测试任务索引包含重复任务")), QList<ModelTestTaskDefinition>{};
            uuids.insert(uuid);
            names.insert(name);
            result.push_back(task);
        }
    }
    catch (const std::exception &e)
    {
        setError(err_msg, QString("读取测试任务索引失败: %1").arg(QString(e.what())));
        return {};
    }
    return result;
}

bool ModelTestTaskRepository::loadTask(const QString &model_name, const QString &uuid,
                                       ModelTestTaskDefinition &task, QString *err_msg) const
{
    const QList<ModelTestTaskDefinition> tasks = listTasks(model_name, err_msg);
    const auto found = std::find_if(tasks.cbegin(), tasks.cend(), [&uuid](const ModelTestTaskDefinition &entry)
                                     { return entry.uuid == uuid.trimmed(); });
    if (found == tasks.cend())
        return setError(err_msg, QString("测试任务不存在: %1").arg(uuid));
    task = *found;
    // 任务目录内的 config.yaml 是参数的最终来源，索引只承担发现和排序职责。
    return readTaskConfig(model_name, task, err_msg);
}

bool ModelTestTaskRepository::ensureTaskRoot(const QString &model_name, const ModelTestTaskDefinition &task,
                                             QString *err_msg) const
{
    ModelStorageService storage(project_dir_);
    if (!storage.ensureTestTaskStorage(model_name, task.directory_name, err_msg))
        return false;
    const QString root = storage.testTaskRoot(model_name, task.directory_name);
    if (root.isEmpty())
        return setError(err_msg, QString("测试任务目录路径非法"));
    return true;
}

bool ModelTestTaskRepository::writeTaskConfig(const QString &model_name, const ModelTestTaskDefinition &task,
                                              QString *err_msg) const
{
    ModelStorageService storage(project_dir_);
    const QString path = storage.testTaskConfigPath(model_name, task.directory_name);
    if (path.isEmpty())
        return setError(err_msg, QString("测试任务配置路径为空"));
    const QVariantMap root = taskToMap(task);
    return common::yaml::writeFileAtomic(path, common::yaml::variantToYaml(root), err_msg,
                                         QString("打开测试任务配置失败"),
                                         QString("生成测试任务配置失败"),
                                         QString("提交测试任务配置失败"));
}

bool ModelTestTaskRepository::readTaskConfig(const QString &model_name, ModelTestTaskDefinition &task,
                                             QString *err_msg) const
{
    ModelStorageService storage(project_dir_);
    const QString path = storage.testTaskConfigPath(model_name, task.directory_name);
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile())
        return setError(err_msg, QString("测试任务配置不存在: %1").arg(path));
    try
    {
        const YAML::Node root = common::yaml::loadFile(file);
        if (!root || !root.IsMap())
            return setError(err_msg, QString("测试任务配置不是 YAML map"));
        QVariantMap root_map = common::yaml::nodeVariant(root).toMap();
        const ModelTestTaskDefinition runtime_config = taskFromMap(root_map);
        if (!validTaskDefinition(runtime_config, err_msg))
            return false;
        if (runtime_config.uuid != task.uuid || runtime_config.model_uuid != task.model_uuid
            || runtime_config.directory_name != task.directory_name)
            return setError(err_msg, QString("测试任务配置与索引不一致"));

        // A runner config may normalize paths in its top-level test_params.
        // The nested task_definition is the durable editable definition and
        // therefore takes precedence when present.  Keeping the runtime map
        // at the top level lets Python consume the file without a second
        // config path while preventing path drift after a restart.
        QVariantMap definition_map = root_map.value(QStringLiteral("task_definition")).toMap();
        ModelTestTaskDefinition config = runtime_config;
        if (!definition_map.isEmpty())
        {
            for (const QString &field : {QStringLiteral("uuid"), QStringLiteral("model_uuid"),
                                         QStringLiteral("name"), QStringLiteral("directory_name"),
                                         QStringLiteral("created_at"), QStringLiteral("modified_at"),
                                         QStringLiteral("test_params"), QStringLiteral("dataset_selection")})
            {
                if (definition_map.contains(field))
                    root_map.insert(field, definition_map.value(field));
            }
            config = taskFromMap(root_map);
            if (!validTaskDefinition(config, err_msg))
                return false;
        }
        if (config.uuid != task.uuid || config.model_uuid != task.model_uuid
            || config.directory_name != task.directory_name)
            return setError(err_msg, QString("测试任务定义与索引不一致"));
        task.name = config.name;
        task.test_params = config.test_params;
        task.dataset_selection = config.dataset_selection;
        task.modified_at = config.modified_at;
    }
    catch (const std::exception &e)
    {
        return setError(err_msg, QString("读取测试任务配置失败: %1").arg(QString(e.what())));
    }
    return true;
}

bool ModelTestTaskRepository::writeIndex(const QString &model_name, const QList<ModelTestTaskDefinition> &tasks,
                                         QString *err_msg,
                                         const std::optional<QString> &current_uuid_override) const
{
    const QString path = tasksPath(model_name);
    if (path.isEmpty())
        return setError(err_msg, QString("测试任务索引路径为空"));
    ModelStorageService storage(project_dir_);
    if (!storage.ensureTestStorage(model_name, err_msg))
        return false;
    QVariantList entries;
    for (const ModelTestTaskDefinition &task : tasks)
        entries.push_back(taskToMap(task));
    QVariantMap root = {{key("schema_version"), 1}, {key("tasks"), entries}};
    const QString selected_uuid = current_uuid_override.has_value()
        ? current_uuid_override.value()
        : currentTaskUuid(model_name, nullptr);
    if (!selected_uuid.isEmpty())
        root.insert(key("current_task_uuid"), selected_uuid);
    return common::yaml::writeFileAtomic(path, common::yaml::variantToYaml(root), err_msg,
                                         QString("打开测试任务索引失败"),
                                         QString("生成测试任务索引失败"),
                                         QString("提交测试任务索引失败"));
}

bool ModelTestTaskRepository::saveTask(const QString &model_name, const ModelTestTaskDefinition &task,
                                       QString *err_msg) const
{
    if (!task.isValid())
        return setError(err_msg, QString("测试任务定义无效"));
    if (const QString error = validateTaskName(task.name); !error.isEmpty())
        return setError(err_msg, error);
    QList<ModelTestTaskDefinition> tasks = listTasks(model_name, err_msg);
    if (err_msg != nullptr && !err_msg->isEmpty())
        return false;

    ModelStorageService storage(project_dir_);
    const QString config_path = storage.testTaskConfigPath(model_name, task.directory_name);
    const QFileInfo old_config_info(config_path);
    const bool had_old_config = old_config_info.exists() && old_config_info.isFile();
    QByteArray old_config;
    if (had_old_config)
    {
        QFile file(config_path);
        if (!file.open(QIODevice::ReadOnly))
            return setError(err_msg, QString("读取旧测试任务配置失败: %1").arg(file.errorString()));
        old_config = file.readAll();
    }

    if (!ensureTaskRoot(model_name, task, err_msg) || !writeTaskConfig(model_name, task, err_msg))
        return false;

    bool found = false;
    for (ModelTestTaskDefinition &entry : tasks)
    {
        if (entry.uuid == task.uuid)
        {
            entry = task;
            found = true;
            break;
        }
    }
    if (!found)
        tasks.push_back(task);
    if (writeIndex(model_name, tasks, err_msg))
        return true;

    // The config and index form one logical commit.  If the index cannot be
    // atomically committed, restore the previous config (or remove a newly
    // created one) so a later load cannot observe mixed task definitions.
    if (had_old_config)
        restoreFile(config_path, old_config);
    else
        QFile::remove(config_path);
    return false;
}

bool ModelTestTaskRepository::createTask(const QString &model_name, const QString &model_uuid, const QString &name,
                                         const QVariantMap &test_params,
                                         const ModelDatasetSelection &dataset_selection,
                                         ModelTestTaskDefinition &task, QString *err_msg) const
{
    if (const QString error = validateTaskName(name); !error.isEmpty())
        return setError(err_msg, error);
    QList<ModelTestTaskDefinition> tasks = listTasks(model_name, err_msg);
    for (const ModelTestTaskDefinition &entry : tasks)
    {
        if (sameName(entry.name, name) || sameName(entry.directory_name, directoryNameForTask(name)))
            return setError(err_msg, QString("测试任务名称已存在: %1").arg(name));
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    task = {};
    task.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    task.model_uuid = model_uuid.trimmed();
    task.name = name.trimmed();
    task.directory_name = directoryNameForTask(task.name);
    task.test_params = test_params;
    task.dataset_selection = dataset_selection;
    task.created_at = now;
    task.modified_at = now;
    if (!task.isValid())
        return setError(err_msg, QString("测试任务定义无效"));

    if (!ensureTaskRoot(model_name, task, err_msg) || !writeTaskConfig(model_name, task, err_msg))
    {
        const QString root = ModelStorageService(project_dir_).testTaskRoot(model_name, task.directory_name);
        if (!root.isEmpty())
            QDir(root).removeRecursively();
        return false;
    }
    tasks.push_back(task);
    // A newly created task is the active editing context.  Persist that
    // selection together with the index so a process restart restores the
    // same task even if the manager has not emitted its selection signal yet.
    if (!writeIndex(model_name, tasks, err_msg, task.uuid))
    {
        QDir(ModelStorageService(project_dir_).testTaskRoot(model_name, task.directory_name)).removeRecursively();
        return false;
    }
    return true;
}

bool ModelTestTaskRepository::renameTask(const QString &model_name, const QString &uuid, const QString &name,
                                         QString *err_msg) const
{
    if (const QString error = validateTaskName(name); !error.isEmpty())
        return setError(err_msg, error);
    QList<ModelTestTaskDefinition> tasks = listTasks(model_name, err_msg);
    auto found = std::find_if(tasks.begin(), tasks.end(), [&uuid](const ModelTestTaskDefinition &entry)
                              { return entry.uuid == uuid.trimmed(); });
    if (found == tasks.end())
        return setError(err_msg, QString("测试任务不存在: %1").arg(uuid));
    for (const ModelTestTaskDefinition &entry : tasks)
    {
        if (entry.uuid != found->uuid && sameName(entry.name, name))
            return setError(err_msg, QString("测试任务名称已存在: %1").arg(name));
    }

    ModelStorageService storage(project_dir_);
    const QString old_root = storage.testTaskRoot(model_name, found->directory_name);
    const QString new_directory = directoryNameForTask(name);
    const QString new_root = storage.testTaskRoot(model_name, new_directory);
    if (old_root.isEmpty() || new_root.isEmpty())
        return setError(err_msg, QString("测试任务目录路径非法"));
    const bool same_directory = old_root.compare(new_root, Qt::CaseSensitive) == 0;
    // Windows 文件系统通常大小写不敏感；改名仅改变大小写时，目标路径
    // 看起来已经存在，必须先经由临时目录才能完成原子切换。
    const bool case_only_rename = !same_directory && old_root.compare(new_root, Qt::CaseInsensitive) == 0;
    QString temporary_directory;
    if (!same_directory && !case_only_rename && QDir(new_root).exists())
        return setError(err_msg, QString("目标测试任务目录已存在: %1").arg(new_directory));

    const QString old_name = found->name;
    const QString old_directory = found->directory_name;
    const QString old_config_path = storage.testTaskConfigPath(model_name, old_directory);
    QByteArray old_config_bytes;
    {
        QFile old_config(old_config_path);
        if (old_config.exists())
        {
            if (!old_config.open(QIODevice::ReadOnly))
                return setError(err_msg, QString("读取旧测试任务配置失败: %1").arg(old_config.errorString()));
            old_config_bytes = old_config.readAll();
        }
    }

    if (!same_directory)
    {
        QDir parent(QFileInfo(old_root).absoluteDir());
        temporary_directory = found->directory_name + QStringLiteral(".rename-tmp-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!parent.rename(found->directory_name, temporary_directory))
            return setError(err_msg, QString("重命名测试任务目录失败"));
        if (!parent.rename(temporary_directory, new_directory))
        {
            parent.rename(temporary_directory, found->directory_name);
            return setError(err_msg, QString("重命名测试任务目录失败"));
        }
    }

    found->name = name.trimmed();
    found->directory_name = new_directory;
    found->modified_at = QDateTime::currentSecsSinceEpoch();
    if (!writeTaskConfig(model_name, *found, err_msg) || !writeIndex(model_name, tasks, err_msg))
    {
        found->name = old_name;
        found->directory_name = old_directory;
        if (!same_directory)
        {
            QDir parent(QFileInfo(old_root).absoluteDir());
            if (case_only_rename)
            {
                const QString rollback_directory = old_directory + QStringLiteral(".rollback-")
                    + QUuid::createUuid().toString(QUuid::WithoutBraces);
                if (parent.rename(new_directory, rollback_directory))
                    parent.rename(rollback_directory, old_directory);
            }
            else
            {
                parent.rename(new_directory, old_directory);
            }
        }
        const QString restore_path = storage.testTaskConfigPath(model_name, old_directory);
        if (!old_config_bytes.isEmpty())
            restoreFile(restore_path, old_config_bytes);
        else
            QFile::remove(restore_path);
        return false;
    }
    return true;
}

bool ModelTestTaskRepository::removeTask(const QString &model_name, const QString &uuid, QString *err_msg) const
{
    QList<ModelTestTaskDefinition> tasks = listTasks(model_name, err_msg);
    const auto found = std::find_if(tasks.cbegin(), tasks.cend(), [&uuid](const ModelTestTaskDefinition &entry)
                                     { return entry.uuid == uuid.trimmed(); });
    if (found == tasks.cend())
        return setError(err_msg, QString("测试任务不存在: %1").arg(uuid));
    const ModelStorageService storage(project_dir_);
    const QString root = storage.testTaskRoot(model_name, found->directory_name);
    const QString current_uuid = currentTaskUuid(model_name, err_msg);
    if (err_msg != nullptr && !err_msg->isEmpty())
        return false;

    // Move the directory out of the indexed namespace first.  The index is
    // committed atomically below; if that write fails, the directory can be
    // moved back and the old index remains valid.
    QString temporary_root;
    bool moved_to_temporary = false;
    if (!root.isEmpty() && QDir(root).exists())
    {
        QDir parent(QFileInfo(root).absoluteDir());
        const QString temporary_directory = found->directory_name + QStringLiteral(".delete-tmp-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        temporary_root = parent.filePath(temporary_directory);
        if (!parent.rename(found->directory_name, temporary_directory))
            return setError(err_msg, QString("移动待删除测试任务目录失败: %1").arg(root));
        moved_to_temporary = true;
    }
    tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [&uuid](const ModelTestTaskDefinition &entry)
                               { return entry.uuid == uuid.trimmed(); }),
                tasks.end());
    const QString replacement = current_uuid == uuid.trimmed()
        ? (tasks.isEmpty() ? QString() : tasks.constFirst().uuid)
        : current_uuid;
    if (!writeIndex(model_name, tasks, err_msg, replacement))
    {
        if (moved_to_temporary)
        {
            QDir parent(QFileInfo(root).absoluteDir());
            const QString temporary_directory = QFileInfo(temporary_root).fileName();
            if (!parent.rename(temporary_directory, found->directory_name))
                setError(err_msg, QString("删除事务回滚失败，目录仍保留于: %1").arg(temporary_root));
        }
        return false;
    }
    if (moved_to_temporary && !QDir(temporary_root).removeRecursively())
    {
        // The index is already committed.  Leaving the private temporary
        // directory is safe and recoverable; it is not visible to the task
        // index and can be cleaned on the next repository open.
        spdlog::warn("删除测试任务临时目录失败: {}", temporary_root.toUtf8().constData());
    }
    return true;
}

bool ModelTestTaskRepository::writeResult(const QString &model_name, const QString &task_directory,
                                          const QVariantMap &result, QString *err_msg) const
{
    ModelStorageService storage(project_dir_);
    const QString path = storage.testTaskResultPath(model_name, task_directory);
    if (path.isEmpty())
        return setError(err_msg, QString("测试结果路径为空"));
    const QString root = storage.testTaskRoot(model_name, task_directory);
    const QString result_parent = common::cleanPath(QFileInfo(path).absolutePath());
    const QString task_root = common::cleanPath(QFileInfo(root).absoluteFilePath());
    if (root.isEmpty() || task_root.isEmpty()
        || (result_parent.compare(task_root, Qt::CaseInsensitive) != 0
            && !result_parent.startsWith(task_root + QStringLiteral("/"), Qt::CaseInsensitive)))
        return setError(err_msg, QString("测试结果路径越界"));
    if (!QDir(QFileInfo(path).absolutePath()).exists()
        && !QDir().mkpath(QFileInfo(path).absolutePath()))
        return setError(err_msg, QString("创建测试结果目录失败"));
    return common::yaml::writeFileAtomic(path, common::yaml::variantToYaml(result), err_msg,
                                         QString("打开测试结果失败"), QString("生成测试结果失败"),
                                         QString("提交测试结果失败"));
}

} // namespace dltool::model
