#include "model/ModelTestTaskManager.h"

#include "data/DataManager.h"
#include "data/DataSelectionTreeModel.h"
#include "data/DatasetViewModelFactory.h"
#include "database/ModelTaskDataBase.h"
#include "model/EvaluationViewModelRegistry.h"
#include "model/IModel.h"
#include "model/IModelConfig.h"
#include "model/IParams.h"
#include "model/ModelEvaluationProtocol.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/TaskManager.h"

#include <spdlog/spdlog.h>

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QQmlEngine>
#include <QSize>
#include <algorithm>
#include <cmath>

namespace dltool::model {

namespace {

constexpr const char *kAutomaticThresholdApplied = "adaptive_threshold_applied";

QString statusText(const TaskManager::Task *task)
{
    if (task == nullptr)
        return {};
    switch (task->status)
    {
    case TaskManager::Pending:
        return QString("等待中");
    case TaskManager::Preparing:
        return QString("准备中");
    case TaskManager::Running:
        return QString("运行中");
    case TaskManager::Paused:
        return QString("已暂停");
    case TaskManager::Stopping:
        return QString("停止中");
    case TaskManager::Stopped:
        return QString("已停止");
    case TaskManager::Finished:
        return QString("已结束");
    case TaskManager::Failed:
        return QString("失败");
    }
    return QString("未知");
}

bool activeTask(const TaskManager::Task *task)
{
    return task != nullptr
        && (task->status == TaskManager::Preparing || task->status == TaskManager::Running
            || task->status == TaskManager::Paused || task->status == TaskManager::Stopping);
}

ModelDatasetSelection readSelection(const data::DataSelectionTreeModel *model)
{
    ModelDatasetSelection result;
    if (model == nullptr)
        return result;
    for (const QVariant &value : model->selectedDatasetClassScope())
    {
        const QVariantMap entry          = value.toMap();
        bool              dataset_ok     = false;
        bool              class_ok       = false;
        const qint64      dataset_id     = entry.value(QStringLiteral("dataset_id")).toLongLong(&dataset_ok);
        const qint64      label_class_id = entry.value(QStringLiteral("label_class_id")).toLongLong(&class_ok);
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
    for (const qint64 dataset_id : selection.dataset_ids) model->setNodeSelected(dataset_id, -1, true);
    for (const auto &[dataset_id, label_class_id] : selection.label_classes)
        model->setNodeSelected(dataset_id, label_class_id, true);
}

/**
 * @brief 判断模型是否属于小样本（few-shot）流程。
 *
 * 小样本框架（如 FS-SAM2）的测试任务没有 UUID 测试任务记录、无评估适配器，
 * 测试任务管理器据此隐藏/禁用普通测试任务入口。能力来源为框架注册表，
 * 不再按框架名字符串比较。
 * @param model_manager 模型管理器（解析深度学习方法与框架注册表）。
 * @param record 模型记录视图。
 * @return 框架注册表标记为 few_shot 时返回 true。
 */
bool isFewShotModel(const QPointer<ModelManager> &model_manager, const ModelManager::ModelRecordView &record)
{
    return model_manager != nullptr && registeredFramework(model_manager->method(), record.framework_name).isFewShot();
}

QString evaluationInputSnapshot(const QString &project_database_path, const QString &dataset_file_list_path,
                                const QString &task_database_path, const QString &prediction_dir)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto addFileInfo = [&hash](const QString &kind, const QFileInfo &info, const QString &relative_path)
    {
        const QByteArray kind_bytes = kind.toUtf8();
        const QByteArray path_bytes = relative_path.toUtf8();
        hash.addData(QByteArrayView(kind_bytes));
        hash.addData(QByteArrayView("\0", 1));
        hash.addData(QByteArrayView(path_bytes));
        hash.addData(QByteArrayView("\0", 1));
        const QByteArray size_bytes = QByteArray::number(info.size());
        hash.addData(QByteArrayView(size_bytes));
        hash.addData(QByteArrayView("\0", 1));
        const QByteArray modified_bytes = QByteArray::number(info.lastModified().toMSecsSinceEpoch());
        hash.addData(QByteArrayView(modified_bytes));
        hash.addData(QByteArrayView("\n", 1));
    };

    const auto addSingleFile = [&addFileInfo](const QString &kind, const QString &path)
    {
        const QFileInfo info(path);
        if (info.exists() && info.isFile())
            addFileInfo(kind, info, info.absoluteFilePath());
        else
            addFileInfo(kind, QFileInfo{}, info.absoluteFilePath());
    };
    addSingleFile(QStringLiteral("project"), project_database_path);
    addSingleFile(QStringLiteral("dataset"), dataset_file_list_path);
    // The task database also owns dataset/class selection. Its file identity
    // must invalidate a retained evaluation even when it has no prediction
    // rows, which is the normal anomaly-TIFF case.
    addSingleFile(QStringLiteral("task"), task_database_path);

    dltool::database::ModelTaskDataBase task_database(task_database_path);
    QHash<qint64, QVariant>            predictions;
    QString                           prediction_error;
    if (task_database.readPredictions(predictions, &prediction_error))
    {
        QList<qint64> image_ids = predictions.keys();
        std::sort(image_ids.begin(), image_ids.end());
        for (const qint64 image_id : image_ids)
        {
            const QByteArray prediction_data
                = QJsonDocument::fromVariant(predictions.value(image_id)).toJson(QJsonDocument::Compact);
            addFileInfo(QStringLiteral("task-prediction"), QFileInfo{}, QString::number(image_id));
            hash.addData(QByteArrayView(prediction_data));
            hash.addData(QByteArrayView("\n", 1));
        }
    }

    const QDir root(prediction_dir);
    QStringList files;
    if (root.exists())
    {
        QDirIterator iterator(prediction_dir, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
            files.push_back(QDir::fromNativeSeparators(root.relativeFilePath(iterator.next())));
    }
    std::sort(files.begin(), files.end());
    for (const QString &relative_path : files)
        addFileInfo(QStringLiteral("prediction"), QFileInfo(root.filePath(relative_path)), relative_path);

    return QString::fromLatin1(hash.result().toHex());
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
    repository_.setProjectDatabasePath(model_manager_ != nullptr ? model_manager_->projectDatabasePath() : QString());
    save_timer_.setSingleShot(true);
    save_timer_.setInterval(350);
    connect(&save_timer_, &QTimer::timeout, this, [this]() { saveCurrentTask(); });
    if (task_manager_ != nullptr)
    {
        connect(task_manager_, &TaskManager::taskStartRequested, this, &ModelTestTaskManager::handleTaskStartRequested);
        connect(task_manager_, &TaskManager::revisionChanged, this, &ModelTestTaskManager::handleTaskRevisionChanged);
    }
}

ModelTestTaskManager::~ModelTestTaskManager()
{
    shutdown();
    flush();
}

void ModelTestTaskManager::shutdown()
{
    for (ModelEvaluationViewModel *evaluation : evaluation_cache_)
    {
        if (evaluation != nullptr)
            evaluation->invalidate(evaluation::ViewState::NotRun);
    }
    ModelEvaluationViewModel::shutdownEvaluationWorkers();
}

int ModelTestTaskManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : tasks_.size();
}

QVariant ModelTestTaskManager::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= tasks_.size())
        return {};
    const ModelTestTaskDefinition &task    = tasks_.at(index.row());
    const TaskManager::Task       *running = task_manager_ != nullptr ? task_manager_->findModelTaskRecord(
                                                                            model_uuid_, ModelTaskType::Test, task.uuid, true)
                                                                      : nullptr;
    switch (role)
    {
    case Qt::DisplayRole:
    case NameRole:
        return task.name;
    case UuidRole:
        return task.uuid;
    case DirectoryNameRole:
        return task.directory_name;
    case CreatedAtRole:
        return task.created_at;
    case ModifiedAtRole:
        return task.modified_at;
    case RunningRole:
        return activeTask(running);
    case ProgressRole:
        return running != nullptr ? running->progress : 0;
    case StatusRole:
        return statusText(running);
    default:
        return {};
    }
}

QHash<int, QByteArray> ModelTestTaskManager::roleNames() const
{
    return {
        {         UuidRole,          "uuid"},
        {         NameRole,          "name"},
        {DirectoryNameRole, "directoryName"},
        {    CreatedAtRole,     "createdAt"},
        {   ModifiedAtRole,    "modifiedAt"},
        {      RunningRole,       "running"},
        {     ProgressRole,      "progress"},
        {       StatusRole,        "status"}
    };
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

bool ModelTestTaskManager::currentModelBusy() const
{
    if (model_uuid_.trimmed().isEmpty())
        return false;
    if (task_manager_ != nullptr && task_manager_->hasActiveModelTasks(model_uuid_))
        return true;
    return current_evaluation_ != nullptr && current_evaluation_->loading();
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
    if (currentModelBusy())
    {
        emit errorOccurred(QString("模型任务运行期间不能创建测试任务"));
        return QString("模型任务运行期间不能创建测试任务");
    }
    if (isFewShotModel(model_manager_, model_manager_->modelRecordViewForUuid(model_uuid_)))
        return QString("FS-SAM2 模型不需要评估任务");
    if (const QString error = validateTaskName(name); !error.isEmpty())
        return error;
    if (!flush())
        return QString("保存当前测试任务失败");

    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(model_uuid_);
    IModel                             *model  = model_manager_->modelForUuid(model_uuid_);
    if (!record.isValid() || model == nullptr || model->config() == nullptr)
        return QString("当前模型不存在");
    QVariantMap params;
    if (const ITestParams *test_params = model->config()->testParams(); test_params != nullptr)
        params = test_params->valuesMap();

    ModelTestTaskDefinition task;
    QString                 error;
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
    if (currentModelBusy())
    {
        emit errorOccurred(QString("模型任务运行期间不能切换测试任务"));
        return false;
    }
    const auto found = std::find_if(tasks_.cbegin(), tasks_.cend(), [&uuid](const ModelTestTaskDefinition &task)
                                    { return task.uuid == uuid.trimmed(); });
    return found != tasks_.cend() && selectIndex(static_cast<int>(std::distance(tasks_.cbegin(), found)), true);
}

bool ModelTestTaskManager::renameTask(const QString &uuid, const QString &name)
{
    if (currentModelBusy())
    {
        emit errorOccurred(QString("模型任务运行期间不能重命名测试任务"));
        return false;
    }
    const int row = std::distance(
        tasks_.cbegin(), std::find_if(tasks_.cbegin(), tasks_.cend(), [&uuid](const ModelTestTaskDefinition &task)
                                      { return task.uuid == uuid.trimmed(); }));
    if (row < 0 || row >= tasks_.size())
        return false;
    const TaskManager::Task *running
        = task_manager_ != nullptr ? task_manager_->findModelTaskRecord(model_uuid_, ModelTaskType::Test, uuid, false)
                                   : nullptr;
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
    tasks_[row].name           = name.trimmed();
    tasks_[row].directory_name = ModelTestTaskRepository::directoryNameForTask(name);
    tasks_[row].modified_at    = QDateTime::currentSecsSinceEpoch();
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
    if (currentModelBusy())
    {
        emit errorOccurred(QString("模型任务运行期间不能删除测试任务"));
        return false;
    }
    const int row = std::distance(
        tasks_.cbegin(), std::find_if(tasks_.cbegin(), tasks_.cend(), [&uuid](const ModelTestTaskDefinition &task)
                                      { return task.uuid == uuid.trimmed(); }));
    if (row < 0 || row >= tasks_.size())
        return false;
    const TaskManager::Task *running
        = task_manager_ != nullptr ? task_manager_->findModelTaskRecord(model_uuid_, ModelTaskType::Test, uuid, false)
                                   : nullptr;
    if (activeTask(running))
    {
        emit errorOccurred(QString("运行中的测试任务不能删除"));
        return false;
    }
    const QString model_name
        = model_manager_ != nullptr ? model_manager_->modelRecordViewForUuid(model_uuid_).name : QString();
    QString error;
    if (!repository_.removeTask(model_name, uuid, &error))
    {
        emit errorOccurred(error);
        return false;
    }
    const QString cache_key = evaluationCacheKey(uuid);
    if (ModelEvaluationViewModel *evaluation = evaluation_cache_.take(cache_key); evaluation != nullptr)
        delete evaluation;
    pending_evaluation_notifications_.remove(cache_key);
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
        emit          currentIndexChanged();
        emit          currentTaskChanged();
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
    emit currentIndexChanged();
    emit currentTaskChanged();
    return true;
}

bool ModelTestTaskManager::saveDefinition(ModelTestTaskDefinition &task, const bool persist_selection)
{
    if (current_test_params_ != nullptr)
        task.test_params = current_test_params_->valuesMap();
    // 数据集选择默认不落库：编辑期间只保存在内存（snapshotCurrentDatasetSelection），
    // 仅当 persist_selection（手动运行测试前的提交）时才从视图读取并持久化。
    if (persist_selection)
        task.dataset_selection = readSelection(current_dataset_view_model_);
    task.modified_at                           = QDateTime::currentSecsSinceEpoch();
    const ModelManager::ModelRecordView record = model_manager_ != nullptr
                                                   ? model_manager_->modelRecordViewForUuid(model_uuid_)
                                                   : ModelManager::ModelRecordView{};
    if (!record.isValid())
        return false;
    QString error;
    if (!repository_.saveTask(record.name, task, persist_selection, &error))
    {
        emit errorOccurred(error);
        return false;
    }
    return true;
}

void ModelTestTaskManager::snapshotCurrentDatasetSelection()
{
    if (current_index_ < 0 || current_index_ >= tasks_.size())
        return;
    // 数据集选择只更新内存任务记录并刷新界面行，不写库。
    tasks_[current_index_].dataset_selection = readSelection(current_dataset_view_model_);
    tasks_[current_index_].modified_at       = QDateTime::currentSecsSinceEpoch();
    emitTaskRowChanged(current_index_);
}

bool ModelTestTaskManager::commitCurrentDatasetSelection()
{
    // 手动运行测试前把当前数据集选择与参数一起提交落库，保证本次运行
    // 使用界面上的最新选择。
    if (current_index_ < 0 || current_index_ >= tasks_.size())
        return true;
    return saveDefinition(tasks_[current_index_], true);
}

bool ModelTestTaskManager::saveCurrentTask()
{
    if (!save_timer_.isActive() && (current_index_ < 0 || current_index_ >= tasks_.size()))
        return true;
    save_timer_.stop();
    if (current_index_ < 0 || current_index_ >= tasks_.size())
        return true;
    if (!saveDefinition(tasks_[current_index_], false))
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

QString ModelTestTaskManager::evaluationCacheKey(const QString &task_uuid) const
{
    return model_uuid_ + QLatin1Char('\x1f') + task_uuid.trimmed();
}

bool ModelTestTaskManager::automaticThresholdApplied(const QString &task_uuid) const
{
    if (model_manager_ == nullptr || task_uuid.trimmed().isEmpty())
        return false;
    const QVariantMap model_record = model_manager_->modelRecordForUuid(model_uuid_);
    const QVariantMap extra_data   = model_record.value(QStringLiteral("extra_data")).toMap();
    const QVariantMap test_tasks   = extra_data.value(QStringLiteral("test_tasks")).toMap();
    return test_tasks.value(task_uuid.trimmed()).toMap().value(QString::fromLatin1(kAutomaticThresholdApplied))
        .toBool();
}

bool ModelTestTaskManager::markAutomaticThresholdApplied(const QString &task_uuid, QString *err_msg)
{
    if (model_manager_ == nullptr || task_uuid.trimmed().isEmpty())
    {
        if (err_msg != nullptr)
            *err_msg = QStringLiteral("模型或测试任务为空");
        return false;
    }

    const QVariantMap model_record = model_manager_->modelRecordForUuid(model_uuid_);
    QVariantMap       extra_data   = model_record.value(QStringLiteral("extra_data")).toMap();
    QVariantMap       test_tasks   = extra_data.value(QStringLiteral("test_tasks")).toMap();
    QVariantMap       task_state   = test_tasks.value(task_uuid.trimmed()).toMap();
    task_state.insert(QString::fromLatin1(kAutomaticThresholdApplied), true);
    test_tasks.insert(task_uuid.trimmed(), task_state);
    return model_manager_->updateModelExtraData(model_uuid_, {{QStringLiteral("test_tasks"), test_tasks}}, err_msg);
}

void ModelTestTaskManager::handleEvaluationCompleted(const QString &cache_key)
{
    if (applying_best_threshold_ || current_evaluation_ == nullptr
        || evaluation_cache_.value(cache_key, nullptr) != current_evaluation_)
        return;

    const QString task_uuid = currentTaskUuid();
    if (task_uuid.isEmpty() || evaluationCacheKey(task_uuid) != cache_key || automaticThresholdApplied(task_uuid))
        return;
    if (!current_evaluation_->hasBestThreshold())
        return;

    const double threshold = current_evaluation_->bestThreshold();
    if (!std::isfinite(threshold))
        return;

    ITestParams *params = current_test_params_.get();
    if (params == nullptr)
        return;
    const QString parameter_name
        = evaluation::isAnomaly(evaluation::fromProjectMethod(model_manager_->method()))
            ? QStringLiteral("classification_threshold")
            : QStringLiteral("conf");
    ParamGroupModel *evaluation_group = nullptr;
    for (QObject *object : params->groupObjects())
    {
        auto *group = qobject_cast<ParamGroupModel *>(object);
        if (group != nullptr && group->nameEn().compare(QStringLiteral("evaluation"), Qt::CaseInsensitive) == 0)
        {
            evaluation_group = group;
            break;
        }
    }
    if (evaluation_group == nullptr)
        return;

    applying_best_threshold_ = true;
    const bool accepted        = evaluation_group->setValueForName(parameter_name, threshold);
    applying_best_threshold_ = false;
    if (!accepted && !qFuzzyCompare(evaluation_group->valueForName(parameter_name).toDouble() + 1.0,
                                    threshold + 1.0))
    {
        spdlog::warn("测试任务 {} 自动应用最佳阈值失败", task_uuid.toUtf8().constData());
        return;
    }

    const double applied_threshold = evaluation_group->valueForName(parameter_name).toDouble();
    QString error;
    if (!flush() || !markAutomaticThresholdApplied(task_uuid, &error))
    {
        spdlog::error("测试任务 {} 保存自动应用最佳阈值状态失败: {}", task_uuid.toUtf8().constData(),
                      error.toUtf8().constData());
        return;
    }

    // 标记首次自动应用会更新项目数据库文件时间。同步该次评估的输入快照，
    // 避免同一结果因本次自身的持久化动作被后续无关任务变化重新触发。
    ModelEvaluationOptions synchronized_options;
    if (buildEvaluationOptions(tasks_.at(current_index_), synchronized_options, &error))
        current_evaluation_->adoptEvaluationThreshold(applied_threshold, synchronized_options.prediction_snapshot);
    else
        spdlog::warn("测试任务 {} 同步自动应用后的评估快照失败: {}", task_uuid.toUtf8().constData(),
                     error.toUtf8().constData());
}

bool ModelTestTaskManager::buildEvaluationOptions(const ModelTestTaskDefinition &task, ModelEvaluationOptions &options,
                                                  QString *err_msg) const
{
    if (model_manager_ == nullptr)
    {
        if (err_msg != nullptr)
            *err_msg = QString("模型管理器为空");
        return false;
    }
    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(model_uuid_);
    if (!record.isValid())
    {
        if (err_msg != nullptr)
            *err_msg = QString("模型不存在");
        return false;
    }
    if (isFewShotModel(model_manager_, record))
    {
        if (err_msg != nullptr)
            *err_msg = QString("FS-SAM2 模型不支持评估");
        return false;
    }
    const ModelStorageService storage(project_dir_);
    options                        = {};
    options.model_uuid             = model_uuid_;
    options.test_task_uuid         = task.uuid;
    options.model_name             = record.name;
    options.task_directory         = task.directory_name;
    options.method                 = evaluation::fromProjectMethod(model_manager_->method());
    options.project_database_path  = model_manager_->projectDatabasePath();
    options.dataset_file_list_path = storage.testTaskFileListPath(record.name, task.directory_name);
    options.task_database_path     = storage.testTaskDatabasePath(record.name, task.directory_name);
    options.prediction_dir         = storage.testTaskPredictionPath(record.name, task.directory_name);
    options.prediction_snapshot
        = evaluationInputSnapshot(options.project_database_path, options.dataset_file_list_path,
                                  options.task_database_path, options.prediction_dir);

    const QVariantMap test_params
        = current_test_params_ != nullptr ? current_test_params_->valuesMap() : task.test_params;
    if (IModel *model = model_manager_->modelForUuid(model_uuid_); model != nullptr && model->config() != nullptr)
    {
        if (const ITrainParams *train_params = model->config()->trainParams(); train_params != nullptr)
            options.preprocessing_config = train_params->valuesMap();
    }
    options.evaluation_config
        = evaluation::normalizedEvaluationConfig(test_params.value(QStringLiteral("evaluation")).toMap());
    // heatmap_threshold is a display-only control. It is read by the QML
    // thumbnail request and must not invalidate or rerun metric evaluation.
    options.evaluation_config.remove(QStringLiteral("heatmap_threshold"));
    if (options.method == evaluation::Method::AnomalyDetection)
    {
        options.confidence_threshold
            = options.evaluation_config
                  .value(QStringLiteral("classification_threshold"), evaluation::kDefaultConfidenceThreshold)
                  .toDouble();
    }
    else
    {
        options.confidence_threshold
            = options.evaluation_config.value(evaluation::fieldName(evaluation::Field::ConfidenceThreshold)).toDouble();
    }
    options.iou_threshold
        = options.evaluation_config.value(evaluation::fieldName(evaluation::Field::IouThreshold)).toDouble();
    options.matching_strategy = evaluation::matchingStrategyFromKey(
        options.evaluation_config.value(evaluation::fieldName(evaluation::Field::MatchingStrategy)).toString());
    // The first successful evaluation is assembled at the searched optimum.
    // The marker is persisted at model extra_data scope; once it exists the
    // user's current evaluation threshold remains authoritative.
    options.apply_best_threshold = !automaticThresholdApplied(task.uuid);
    // 复用 DataManager 后台预取的图像尺寸缓存,评估线程不再逐张打开图像文件。
    options.image_dimensions_provider = [this](const qint64 image_id, int *width, int *height) -> bool
    {
        if (data_manager_ == nullptr)
            return false;
        const QSize size = data_manager_->imageSize(image_id);
        if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
            return false;
        if (width != nullptr)
            *width = size.width();
        if (height != nullptr)
            *height = size.height();
        return true;
    };
    return true;
}

void ModelTestTaskManager::handleParameterChanged(const QString &group_name, const QString &parameter_name)
{
    scheduleSave();
    if (model_manager_ != nullptr
        && isFewShotModel(model_manager_, model_manager_->modelRecordViewForUuid(model_uuid_)))
        return;
    if (current_evaluation_ == nullptr || current_index_ < 0 || current_index_ >= tasks_.size())
        return;
    if (applying_best_threshold_)
        return;

    const bool evaluation_changed = group_name.compare(QStringLiteral("evaluation"), Qt::CaseInsensitive) == 0;
    const bool heatmap_changed
        = parameter_name.compare(QStringLiteral("heatmap_threshold"), Qt::CaseInsensitive) == 0;
    if (evaluation_changed && !heatmap_changed)
    {
        ModelEvaluationOptions options;
        QString                error;
        if (!buildEvaluationOptions(tasks_.at(current_index_), options, &error))
        {
            spdlog::error("更新测试评估参数失败: {}", error.toUtf8().constData());
            current_evaluation_->invalidate();
            return;
        }
        current_evaluation_->setEvaluationOptions(options);
        if (!current_evaluation_->hasPredictionResults())
        {
            emit taskStateChanged();
            return;
        }
        const TaskManager::Task *task = currentTaskRecord();
        if (task == nullptr || task->status == TaskManager::Finished)
            current_evaluation_->evaluate(false);
        return;
    }

    // Inference parameters affect the next prediction run.  Keep the current
    // evaluation visible until that run is explicitly started; the start
    // request invalidates it before new predictions are generated.
}

void ModelTestTaskManager::handleTaskRevisionChanged()
{
    if (!tasks_.isEmpty())
    {
        emit dataChanged(index(0), index(tasks_.size() - 1), {RunningRole, ProgressRole, StatusRole});
    }
    if (current_evaluation_ != nullptr
        && (model_manager_ == nullptr
            || !isFewShotModel(model_manager_, model_manager_->modelRecordViewForUuid(model_uuid_))))
    {
        const TaskManager::Task *task = currentTaskRecord();
        if (task == nullptr)
        {
            // A test task does not need a TaskManager record until it is
            // started.  Keep an already cached evaluation visible while
            // unrelated task records change.
        }
        else if (task->status == TaskManager::Finished)
        {
            if (current_index_ >= 0 && current_index_ < tasks_.size())
            {
                ModelEvaluationOptions options;
                QString                error;
                if (buildEvaluationOptions(tasks_.at(current_index_), options, &error))
                    current_evaluation_->setEvaluationOptions(options);
                else
                    spdlog::error("准备测试任务 {} 的评估输入失败: {}", currentTaskUuid().toUtf8().constData(),
                                  error.toUtf8().constData());
            }
            const QString cache_key = evaluationCacheKey(currentTaskUuid());
            const bool    notify    = pending_evaluation_notifications_.contains(cache_key);
            const bool needs_evaluation = notify
                || current_evaluation_->stateKind() == ModelEvaluationViewModel::NotRun;
            if (needs_evaluation)
                current_evaluation_->evaluate(notify);
            pending_evaluation_notifications_.remove(cache_key);
        }
        else if (task->status == TaskManager::Failed)
        {
            current_evaluation_->setRuntimeState(evaluation::ViewState::Failed);
            pending_evaluation_notifications_.remove(evaluationCacheKey(currentTaskUuid()));
        }
        else if (activeTask(task))
            current_evaluation_->setRuntimeState(evaluation::ViewState::Running);
        else if (task->status == TaskManager::Stopped)
        {
            current_evaluation_->setRuntimeState(evaluation::ViewState::NotRun);
            pending_evaluation_notifications_.remove(evaluationCacheKey(currentTaskUuid()));
        }
    }
    emit taskStateChanged();
}

void ModelTestTaskManager::handleTaskStartRequested(const int task_id)
{
    if (task_manager_ == nullptr)
        return;
    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr || !isTestModelTask(task->type) || task->scope_uuid.trimmed().isEmpty())
        return;
    if (model_manager_ != nullptr
        && isFewShotModel(model_manager_, model_manager_->modelRecordViewForUuid(task->model_uuid)))
        return;

    // taskStartRequested is emitted only after an explicit start request has
    // been accepted by TaskManager.  Invalidate a cached evaluation even when
    // this task is not the currently visible one; otherwise selecting it after
    // the run finishes could still expose the previous in-memory result.
    const QString cache_key = task->model_uuid + QLatin1Char('\x1f') + task->scope_uuid.trimmed();
    pending_evaluation_notifications_.insert(cache_key);
    if (ModelEvaluationViewModel *evaluation = evaluation_cache_.value(cache_key, nullptr))
        evaluation->invalidate(evaluation::ViewState::NotRun);
}

void ModelTestTaskManager::reload()
{
    const auto replaceTasks = [this](QList<ModelTestTaskDefinition> tasks)
    {
        beginResetModel();
        tasks_         = std::move(tasks);
        current_index_ = -1;
        endResetModel();
        emit countChanged();
    };

    // QML may still be bound to currentEvaluation while reload() is rebuilding
    // the task/model context.  Synchronous deletion emits destroyed() in the
    // middle of the TableView model reset and leaves Qt Quick polishing a view
    // whose sync model is already gone.  Defer destruction until the current
    // task bindings have received currentTaskChanged.
    for (ModelEvaluationViewModel *evaluation : evaluation_cache_)
    {
        if (evaluation != nullptr)
            evaluation->deleteLater();
    }
    evaluation_cache_.clear();
    pending_evaluation_notifications_.clear();

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
    if (isFewShotModel(model_manager_, record))
    {
        emit currentIndexChanged();
        emit currentTaskChanged();
        return;
    }
    QString                        error;
    QList<ModelTestTaskDefinition> loaded_tasks = repository_.listTasks(record.name, &error);

    // model.db only stores the task index.  Hydrate every task from its own
    // task.db before publishing it, so reopening a project restores the
    // saved parameters and dataset/class selection used by the evaluator.
    if (error.isEmpty())
    {
        for (ModelTestTaskDefinition &task : loaded_tasks)
        {
            ModelTestTaskDefinition hydrated;
            QString                 hydration_error;
            if (!repository_.loadTask(record.name, task.uuid, hydrated, &hydration_error))
            {
                error = hydration_error.isEmpty() ? QString("读取测试任务数据库失败: %1").arg(task.name)
                                                  : hydration_error;
                break;
            }
            hydrated.model_uuid = model_uuid_;
            task                = std::move(hydrated);
        }
    }
    if (!error.isEmpty())
    {
        replaceTasks({});
        emit errorOccurred(error);
        emit currentIndexChanged();
        emit currentTaskChanged();
        return;
    }
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
    selectIndex(0, false);
}

void ModelTestTaskManager::clearCurrentObjects()
{
    current_test_params_.reset();
    if (current_dataset_view_model_ != nullptr)
    {
        // Keep the old selection model alive until QML has rebound
        // currentDatasetViewModel on currentTaskChanged.
        current_dataset_view_model_->deleteLater();
        current_dataset_view_model_ = nullptr;
    }
    // Evaluation view models are intentionally retained in evaluation_cache_
    // so switching test tasks does not rerun an unchanged evaluation.
    current_evaluation_ = nullptr;
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
    {
        const QString model_name = model_manager_->modelRecordViewForUuid(model_uuid_).name;
        current_test_params_->setWeightContext(project_dir_, model_manager_->projectDatabasePath(),
                                               model->frameworkName(), model->modelArchitecture(), model_name);
        current_test_params_->setValuesMap(tasks_.at(current_index_).test_params);
    }
    if (data_manager_ != nullptr)
    {
        current_dataset_view_model_ = data::DatasetViewModelFactory::createDatasetSelectionModel(data_manager_, this);
        applySelection(current_dataset_view_model_, tasks_.at(current_index_).dataset_selection);
    }
    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(model_uuid_);
    if (record.isValid())
    {
        if (isFewShotModel(model_manager_, record))
            return;
        const QString cache_key = evaluationCacheKey(tasks_.at(current_index_).uuid);
        current_evaluation_     = evaluation_cache_.value(cache_key, nullptr);
        if (current_evaluation_ == nullptr)
        {
            current_evaluation_ = EvaluationViewModelRegistry::instance().createViewModel(
                static_cast<evaluation::Method>(model->method()), this);
            if (current_evaluation_ == nullptr)
                return;
            evaluation_cache_.insert(cache_key, current_evaluation_);
            connect(current_evaluation_, &ModelEvaluationViewModel::loadingChanged, this,
                    &ModelTestTaskManager::taskStateChanged);
            connect(current_evaluation_, &ModelEvaluationViewModel::evaluationCompleted, this,
                    [this, cache_key]() { handleEvaluationCompleted(cache_key); });
        }
        if (data_manager_ != nullptr && data_manager_->globalFilter() != nullptr)
            current_evaluation_->setGlobalFilter(data_manager_->globalFilter());

        ModelEvaluationOptions options;
        QString                evaluation_error;
        if (buildEvaluationOptions(tasks_.at(current_index_), options, &evaluation_error))
        {
            current_evaluation_->setEvaluationOptions(options);
            const TaskManager::Task *task = currentTaskRecord();
            if (task != nullptr && activeTask(task))
                current_evaluation_->setRuntimeState(evaluation::ViewState::Running);
            else if (task != nullptr && task->status == TaskManager::Failed)
                current_evaluation_->setRuntimeState(evaluation::ViewState::Failed);
            else if (task != nullptr && task->status == TaskManager::Stopped)
                current_evaluation_->setRuntimeState(evaluation::ViewState::NotRun);

            // Evaluation is lazy: reopening the project or switching tasks
            // only binds the inputs.  The evaluation panel requests the first
            // evaluation when it becomes visible, parameter changes re-evaluate
            // in handleParameterChanged(), and a finished test run re-evaluates
            // in handleTaskRevisionChanged().  A pending notification means the
            // user explicitly started this task and it has since finished, so
            // evaluate with notification even if the panel is not visible yet.
            const bool notify = pending_evaluation_notifications_.contains(cache_key);
            if (notify)
            {
                pending_evaluation_notifications_.remove(cache_key);
                current_evaluation_->evaluate(true);
            }
        }
        else
        {
            spdlog::error("准备测试任务 {} 的评估上下文失败: {}", tasks_.at(current_index_).uuid.toUtf8().constData(),
                          evaluation_error.toUtf8().constData());
            current_evaluation_->setRuntimeState(evaluation::ViewState::NotRun);
        }
    }
    if (current_test_params_ != nullptr)
    {
        for (QObject *object : current_test_params_->groupObjects())
        {
            if (auto *group = qobject_cast<ParamGroupModel *>(object))
                connect(group, &ParamGroupModel::valueChanged, this,
                        [this, group](const QString &parameter_name, const QVariant &)
                        { handleParameterChanged(group->nameEn(), parameter_name); });
        }
    }
    if (current_dataset_view_model_ != nullptr)
    {
        // 数据集选择只更新内存并刷新界面，不落库；仅在手动运行测试时由
        // commitCurrentDatasetSelection() 提交。同时不失效已缓存的评估结果。
        connect(current_dataset_view_model_, &data::DataSelectionTreeModel::selectionChanged, this,
                &ModelTestTaskManager::snapshotCurrentDatasetSelection);
    }
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
