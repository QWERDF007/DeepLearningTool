#include "model/ModelTestTaskManager.h"

#include "data/DataManager.h"
#include "data/DataSelectionTreeModel.h"
#include "data/DatasetViewModelFactory.h"
#include "model/IModel.h"
#include "model/IModelConfig.h"
#include "model/IParams.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/ModelStorageMigration.h"
#include "model/TaskManager.h"

#include <QQmlEngine>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

namespace dltool::model {

namespace {

QString statusText(const TaskManager::Task *task)
{
    if (task == nullptr)
        return {};
    switch (task->status)
    {
    case TaskManager::Pending: return QString("等待中");
    case TaskManager::Preparing: return QString("准备中");
    case TaskManager::Running: return QString("运行中");
    case TaskManager::Paused: return QString("已暂停");
    case TaskManager::Stopping: return QString("停止中");
    case TaskManager::Stopped: return QString("已停止");
    case TaskManager::Finished: return QString("已结束");
    case TaskManager::Failed: return QString("失败");
    }
    return QString("未知");
}

bool activeTask(const TaskManager::Task *task)
{
    return task != nullptr && (task->status == TaskManager::Pending || task->status == TaskManager::Preparing
                               || task->status == TaskManager::Running || task->status == TaskManager::Paused
                               || task->status == TaskManager::Stopping);
}

ModelDatasetSelection readSelection(const data::DataSelectionTreeModel *model)
{
    ModelDatasetSelection result;
    if (model == nullptr)
        return result;
    for (const QVariant &value : model->selectedDatasetClassScope())
    {
        const QVariantMap entry = value.toMap();
        bool dataset_ok = false;
        bool class_ok = false;
        const qint64 dataset_id = entry.value(QStringLiteral("dataset_id")).toLongLong(&dataset_ok);
        const qint64 label_class_id = entry.value(QStringLiteral("label_class_id")).toLongLong(&class_ok);
        if (!dataset_ok || dataset_id < 0)
            continue;
        if (!class_ok || label_class_id < 0)
            result.dataset_ids.insert(dataset_id);
        else
            result.label_classes.insert({dataset_id, label_class_id});
    }
    return result;
}

void applySelection(data::DataSelectionTreeModel *model, const ModelDatasetSelection &selection)
{
    if (model == nullptr)
        return;
    model->clearSelection();
    for (const qint64 dataset_id : selection.dataset_ids)
        model->setNodeSelected(dataset_id, -1, true);
    for (const auto &[dataset_id, label_class_id] : selection.label_classes)
        model->setNodeSelected(dataset_id, label_class_id, true);
}

} // namespace

ModelTestTaskManager::ModelTestTaskManager(QString project_dir, ModelManager *model_manager,
                                           dltool::data::DataManager *data_manager, TaskManager *task_manager,
                                           QObject *parent)
    : QAbstractListModel(parent)
    , project_dir_(std::move(project_dir))
    , model_manager_(model_manager)
    , data_manager_(data_manager)
    , task_manager_(task_manager)
    , repository_(project_dir_)
{
    save_timer_.setSingleShot(true);
    save_timer_.setInterval(350);
    connect(&save_timer_, &QTimer::timeout, this, [this]() { saveCurrentTask(); });
    if (task_manager_ != nullptr)
        connect(task_manager_, &TaskManager::revisionChanged, this, &ModelTestTaskManager::handleTaskRevisionChanged);
}

ModelTestTaskManager::~ModelTestTaskManager()
{
    flush();
}

int ModelTestTaskManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : tasks_.size();
}

QVariant ModelTestTaskManager::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= tasks_.size())
        return {};
    const ModelTestTaskDefinition &task = tasks_.at(index.row());
    const TaskManager::Task *running = task_manager_ != nullptr ? task_manager_->findModelTaskRecord(
        model_uuid_, ModelTaskType::Test, task.uuid, true) : nullptr;
    switch (role)
    {
    case Qt::DisplayRole:
    case NameRole: return task.name;
    case UuidRole: return task.uuid;
    case DirectoryNameRole: return task.directory_name;
    case CreatedAtRole: return task.created_at;
    case ModifiedAtRole: return task.modified_at;
    case RunningRole: return activeTask(running);
    case ProgressRole: return running != nullptr ? running->progress : 0;
    case StatusRole: return statusText(running);
    default: return {};
    }
}

QHash<int, QByteArray> ModelTestTaskManager::roleNames() const
{
    return {{UuidRole, "uuid"}, {NameRole, "name"}, {DirectoryNameRole, "directoryName"},
            {CreatedAtRole, "createdAt"}, {ModifiedAtRole, "modifiedAt"}, {RunningRole, "running"},
            {ProgressRole, "progress"}, {StatusRole, "status"}};
}

QString ModelTestTaskManager::modelUuid() const
{
    return model_uuid_;
}

void ModelTestTaskManager::setModelUuid(const QString &uuid)
{
    const QString value = uuid.trimmed();
    if (model_uuid_ == value)
        return;
    flush();
    model_uuid_ = value;
    emit modelUuidChanged();
    reload();
}

int ModelTestTaskManager::currentIndex() const
{
    return current_index_;
}

int ModelTestTaskManager::count() const
{
    return tasks_.size();
}

QString ModelTestTaskManager::currentTaskUuid() const
{
    return current_index_ >= 0 && current_index_ < tasks_.size() ? tasks_.at(current_index_).uuid : QString();
}

QString ModelTestTaskManager::currentTaskName() const
{
    return current_index_ >= 0 && current_index_ < tasks_.size() ? tasks_.at(current_index_).name : QString();
}

QString ModelTestTaskManager::currentTaskDirectory() const
{
    return current_index_ >= 0 && current_index_ < tasks_.size() ? tasks_.at(current_index_).directory_name : QString();
}

ITestParams *ModelTestTaskManager::currentTestParams() const
{
    return current_test_params_.get();
}

dltool::data::DataSelectionTreeModel *ModelTestTaskManager::currentDatasetViewModel() const
{
    return current_dataset_view_model_;
}

ModelEvaluationViewModel *ModelTestTaskManager::currentEvaluation() const
{
    return current_evaluation_;
}

const TaskManager::Task *ModelTestTaskManager::currentTaskRecord() const
{
    if (task_manager_ == nullptr)
        return nullptr;
    return task_manager_->findModelTaskRecord(model_uuid_, ModelTaskType::Test, currentTaskUuid(), true);
}

bool ModelTestTaskManager::currentTaskRunning() const
{
    return activeTask(currentTaskRecord());
}

int ModelTestTaskManager::currentTaskProgress() const
{
    const TaskManager::Task *task = currentTaskRecord();
    return task != nullptr ? task->progress : 0;
}

QString ModelTestTaskManager::currentTaskStatus() const
{
    return statusText(currentTaskRecord());
}

QString ModelTestTaskManager::validateTaskName(const QString &name) const
{
    const QString error = ModelTestTaskRepository::validateTaskName(name);
    if (!error.isEmpty())
        return error;
    for (const ModelTestTaskDefinition &task : tasks_)
    {
        if (task.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return QString("测试任务名称已存在: %1").arg(name);
    }
    return {};
}

QString ModelTestTaskManager::createTask(const QString &name)
{
    if (model_manager_ == nullptr || model_uuid_.isEmpty())
        return QString("当前模型为空");
    if (const QString error = validateTaskName(name); !error.isEmpty())
        return error;
    if (!flush())
        return QString("保存当前测试任务失败");

    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(model_uuid_);
    IModel *model = model_manager_->modelForUuid(model_uuid_);
    if (!record.isValid() || model == nullptr || model->config() == nullptr)
        return QString("当前模型不存在");
    QVariantMap params;
    if (const ITestParams *test_params = model->config()->testParams(); test_params != nullptr)
        params = test_params->valuesMap();

    ModelTestTaskDefinition task;
    QString error;
    if (!repository_.createTask(record.name, model_uuid_, name, params, {}, task, &error))
        return error;
    const int row = tasks_.size();
    beginInsertRows({}, row, row);
    tasks_.push_back(task);
    endInsertRows();
    emit countChanged();
    selectIndex(row, false);
    return task.uuid;
}

bool ModelTestTaskManager::switchTask(const QString &uuid)
{
    const auto found = std::find_if(tasks_.cbegin(), tasks_.cend(), [&uuid](const ModelTestTaskDefinition &task)
                                     { return task.uuid == uuid.trimmed(); });
    return found != tasks_.cend() && selectIndex(static_cast<int>(std::distance(tasks_.cbegin(), found)), true);
}

bool ModelTestTaskManager::renameTask(const QString &uuid, const QString &name)
{
    const int row = std::distance(tasks_.cbegin(), std::find_if(
                                      tasks_.cbegin(), tasks_.cend(), [&uuid](const ModelTestTaskDefinition &task)
                                      { return task.uuid == uuid.trimmed(); }));
    if (row < 0 || row >= tasks_.size())
        return false;
    const TaskManager::Task *running = task_manager_ != nullptr
        ? task_manager_->findModelTaskRecord(model_uuid_, ModelTaskType::Test, uuid, false) : nullptr;
    if (activeTask(running))
    {
        emit errorOccurred(QString("运行中的测试任务不能重命名"));
        return false;
    }
    for (int i = 0; i < tasks_.size(); ++i)
    {
        if (i != row && tasks_.at(i).name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return false;
    }
    QString error;
    if (!repository_.renameTask(model_manager_->modelRecordViewForUuid(model_uuid_).name, uuid, name, &error))
    {
        emit errorOccurred(error);
        return false;
    }
    tasks_[row].name = name.trimmed();
    tasks_[row].directory_name = ModelTestTaskRepository::directoryNameForTask(name);
    tasks_[row].modified_at = QDateTime::currentSecsSinceEpoch();
    emitTaskRowChanged(row);
    if (row == current_index_)
    {
        bindCurrentObjects();
        emit currentTaskChanged();
    }
    return true;
}

bool ModelTestTaskManager::deleteTask(const QString &uuid)
{
    const int row = std::distance(tasks_.cbegin(), std::find_if(
                                      tasks_.cbegin(), tasks_.cend(), [&uuid](const ModelTestTaskDefinition &task)
                                      { return task.uuid == uuid.trimmed(); }));
    if (row < 0 || row >= tasks_.size())
        return false;
    const TaskManager::Task *running = task_manager_ != nullptr
        ? task_manager_->findModelTaskRecord(model_uuid_, ModelTaskType::Test, uuid, false) : nullptr;
    if (activeTask(running))
    {
        emit errorOccurred(QString("运行中的测试任务不能删除"));
        return false;
    }
    const QString model_name = model_manager_ != nullptr ? model_manager_->modelRecordViewForUuid(model_uuid_).name : QString();
    QString error;
    if (!repository_.removeTask(model_name, uuid, &error))
    {
        emit errorOccurred(error);
        return false;
    }
    const bool deleted_current = row == current_index_;
    beginRemoveRows({}, row, row);
    tasks_.removeAt(row);
    endRemoveRows();
    emit countChanged();

    // A model must always expose an editable test context.  Recreate the
    // first task immediately after deleting the last one instead of leaving
    // QML with a null parameter/data/evaluation binding.
    if (tasks_.isEmpty())
    {
        current_index_ = -1;
        clearCurrentObjects();
        emit currentIndexChanged();
        emit currentTaskChanged();
        const QString created = createTask(QString("测试 1"));
        if (created.isEmpty())
            emit errorOccurred(QString("删除后创建默认测试任务失败"));
        return !created.isEmpty();
    }
    if (row < current_index_)
        --current_index_;
    if (current_index_ >= tasks_.size())
        current_index_ = tasks_.isEmpty() ? -1 : tasks_.size() - 1;
    if (deleted_current || current_index_ < 0)
        bindCurrentObjects();
    if (model_manager_ != nullptr)
    {
        const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(model_uuid_);
        if (record.isValid())
            repository_.setCurrentTaskUuid(record.name, currentTaskUuid(), nullptr);
    }
    emit currentIndexChanged();
    emit currentTaskChanged();
    return true;
}

bool ModelTestTaskManager::saveDefinition(ModelTestTaskDefinition &task)
{
    if (current_test_params_ != nullptr)
        task.test_params = current_test_params_->valuesMap();
    task.dataset_selection = readSelection(current_dataset_view_model_);
    task.modified_at = QDateTime::currentSecsSinceEpoch();
    const ModelManager::ModelRecordView record = model_manager_ != nullptr
        ? model_manager_->modelRecordViewForUuid(model_uuid_) : ModelManager::ModelRecordView{};
    if (!record.isValid())
        return false;
    QString error;
    if (!repository_.saveTask(record.name, task, &error))
    {
        emit errorOccurred(error);
        return false;
    }
    return true;
}

bool ModelTestTaskManager::saveCurrentTask()
{
    if (!save_timer_.isActive() && (current_index_ < 0 || current_index_ >= tasks_.size()))
        return true;
    save_timer_.stop();
    if (current_index_ < 0 || current_index_ >= tasks_.size())
        return true;
    if (!saveDefinition(tasks_[current_index_]))
        return false;
    emitTaskRowChanged(current_index_);
    return true;
}

bool ModelTestTaskManager::flush()
{
    save_timer_.stop();
    return saveCurrentTask();
}

int ModelTestTaskManager::taskId(const QString &uuid) const
{
    const QString value = uuid.trimmed().isEmpty() ? currentTaskUuid() : uuid.trimmed();
    if (task_manager_ == nullptr || model_uuid_.isEmpty() || value.isEmpty())
        return -1;
    return task_manager_->findModelTask(model_uuid_, ModelTaskType::Test, value, true);
}

void ModelTestTaskManager::scheduleSave()
{
    if (current_index_ >= 0)
        save_timer_.start();
}

void ModelTestTaskManager::handleTaskRevisionChanged()
{
    if (!tasks_.isEmpty())
    {
        emit dataChanged(index(0), index(tasks_.size() - 1), {RunningRole, ProgressRole, StatusRole});
    }
    if (current_evaluation_ != nullptr)
    {
        const TaskManager::Task *task = currentTaskRecord();
        if (task == nullptr)
            current_evaluation_->setRuntimeState(QStringLiteral("NotRun"));
        else if (task->status == TaskManager::Finished)
            current_evaluation_->reload();
        else if (task->status == TaskManager::Failed)
            current_evaluation_->setRuntimeState(QStringLiteral("Failed"));
        else if (activeTask(task))
            current_evaluation_->setRuntimeState(QStringLiteral("Running"));
        else if (task->status == TaskManager::Stopped)
            current_evaluation_->setRuntimeState(QStringLiteral("NotRun"));
    }
    emit taskStateChanged();
}

void ModelTestTaskManager::reload()
{
    const auto replaceTasks = [this](QList<ModelTestTaskDefinition> tasks)
    {
        beginResetModel();
        tasks_ = std::move(tasks);
        current_index_ = -1;
        endResetModel();
        emit countChanged();
    };

    replaceTasks({});
    clearCurrentObjects();
    if (model_uuid_.isEmpty() || model_manager_ == nullptr)
    {
        emit currentIndexChanged();
        emit currentTaskChanged();
        return;
    }

    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(model_uuid_);
    if (!record.isValid())
        return;
    const ModelStorageMigrationResult migration
        = migrateModelStorage(project_dir_, record.name, record.uuid);
    if (!migration.error.isEmpty())
    {
        emit errorOccurred(migration.error);
        return;
    }
    QString error;
    QList<ModelTestTaskDefinition> loaded_tasks = repository_.listTasks(record.name, &error);
    if (!error.isEmpty())
        emit errorOccurred(error);
    // The initial reset above intentionally leaves the model empty while
    // storage/migration is read.  Publish the loaded rows with a second
    // model reset; assigning tasks_ directly would leave QML views with the
    // old zero row count until a later add/delete operation happened.
    replaceTasks(std::move(loaded_tasks));
    if (tasks_.isEmpty())
    {
        const QString created = createTask(QString("测试 1"));
        if (created.isEmpty() || created.startsWith(QString("当前")) || created.contains(QString("失败")))
            emit errorOccurred(created);
        return;
    }
    const QString selected_uuid = repository_.currentTaskUuid(record.name, nullptr);
    const auto selected = std::find_if(tasks_.cbegin(), tasks_.cend(), [&selected_uuid](const auto &task)
                                       { return !selected_uuid.isEmpty() && task.uuid == selected_uuid; });
    selectIndex(selected == tasks_.cend() ? 0 : static_cast<int>(std::distance(tasks_.cbegin(), selected)), false);
}

void ModelTestTaskManager::clearCurrentObjects()
{
    current_test_params_.reset();
    if (current_dataset_view_model_ != nullptr)
    {
        delete current_dataset_view_model_;
        current_dataset_view_model_ = nullptr;
    }
    if (current_evaluation_ != nullptr)
    {
        delete current_evaluation_;
        current_evaluation_ = nullptr;
    }
}

void ModelTestTaskManager::bindCurrentObjects()
{
    clearCurrentObjects();
    if (current_index_ < 0 || current_index_ >= tasks_.size() || model_manager_ == nullptr)
        return;
    IModel *model = model_manager_->modelForUuid(model_uuid_);
    if (model == nullptr || model->config() == nullptr)
        return;
    if (const ITestParams *template_params = model->config()->testParams(); template_params != nullptr)
        current_test_params_ = template_params->cloneTestParams();
    if (current_test_params_ != nullptr)
        current_test_params_->setValuesMap(tasks_.at(current_index_).test_params);
    if (data_manager_ != nullptr)
    {
        current_dataset_view_model_ = data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager_, this);
        applySelection(current_dataset_view_model_, tasks_.at(current_index_).dataset_selection);
    }
    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(model_uuid_);
    if (record.isValid())
    {
        const ModelStorageService storage(project_dir_);
        const QString evaluation_dir = storage.testTaskEvaluationPath(record.name, tasks_.at(current_index_).directory_name);
        current_evaluation_ = new ModelEvaluationViewModel(this);
        if (data_manager_ != nullptr && data_manager_->globalFilter() != nullptr)
            current_evaluation_->setGlobalFilter(data_manager_->globalFilter());
        const QString result_path = storage.testTaskResultPath(record.name, tasks_.at(current_index_).directory_name);
        // Keep the expected paths even before the first run.  The task manager
        // can then reload the same view as soon as the background evaluation
        // creates result.yaml/report.yaml, instead of requiring a restart to
        // bind paths that did not exist when the task was selected.
        current_evaluation_->setResultPath(result_path);
        current_evaluation_->setReportPath(QDir(evaluation_dir).filePath(QStringLiteral("report.yaml")));
    }
    if (current_test_params_ != nullptr)
    {
        for (QObject *object : current_test_params_->groupObjects())
        {
            if (auto *group = qobject_cast<ParamGroupModel *>(object))
                connect(group, &ParamGroupModel::valueChanged, this, &ModelTestTaskManager::scheduleSave);
        }
    }
    if (current_dataset_view_model_ != nullptr)
        connect(current_dataset_view_model_, &data::DataSelectionTreeModel::selectionChanged, this,
                &ModelTestTaskManager::scheduleSave);
}

bool ModelTestTaskManager::selectIndex(const int index, const bool save_before)
{
    if (index < 0 || index >= tasks_.size())
        return false;
    if (save_before && !flush())
        return false;
    if (current_index_ == index)
    {
        bindCurrentObjects();
        emit currentTaskChanged();
        emit taskStateChanged();
        return true;
    }
    current_index_ = index;
    bindCurrentObjects();
    if (model_manager_ != nullptr)
    {
        const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(model_uuid_);
        if (record.isValid())
            repository_.setCurrentTaskUuid(record.name, currentTaskUuid(), nullptr);
    }
    emit currentIndexChanged();
    emit currentTaskChanged();
    emit taskStateChanged();
    return true;
}

void ModelTestTaskManager::emitTaskRowChanged(const int row)
{
    if (row >= 0 && row < tasks_.size())
        emit dataChanged(index(row), index(row));
}

} // namespace dltool::model
