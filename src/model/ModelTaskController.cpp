#include "model/ModelTaskController.h"

#include "common/Utils.h"
#include "data/DataManager.h"
#include "model/ExternalModelTaskRunner.h"
#include "model/IModel.h"
#include "model/ModelManager.h"
#include "model/ModelTaskPreparationService.h"
#include "model/TaskManager.h"

#include <spdlog/spdlog.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <utility>

namespace dltool::model {
using common::setError;

ModelTaskController::ModelTaskController(int method, QString project_dir, ModelManager *model_manager,
                                         dltool::data::DataManager *data_manager, TaskManager *task_manager,
                                         QObject *parent)
    : QObject(parent)
    , method_(method)
    , project_dir_(std::move(project_dir))
    , model_manager_(model_manager)
    , data_manager_(data_manager)
    , task_manager_(task_manager)
    , external_task_runner_(std::make_unique<ExternalModelTaskRunner>(this))
{
    connect(external_task_runner_.get(), &ExternalModelTaskRunner::taskFinished, this,
            &ModelTaskController::handleExternalTaskFinished);
    if (task_manager_ != nullptr)
    {
        connect(task_manager_, &TaskManager::taskStopRequested, this, &ModelTaskController::handleTaskStopRequested);
    }
}

ModelTaskController::~ModelTaskController() = default;

int ModelTaskController::addModelTask(const QString &model_uuid, ModelTaskType task_type)
{
    ModelTaskContext context;
    QString          err_msg;
    if (!buildContext(model_uuid, task_type, -1, context, &err_msg))
    {
        spdlog::error("添加模型任务失败: {}", err_msg.toUtf8().constData());
        return -1;
    }
    return ensureTaskRecord(context);
}

int ModelTaskController::startModelTask(const QString &model_uuid, ModelTaskType task_type)
{
    ModelTaskContext context;
    QString          err_msg;
    if (!buildContext(model_uuid, task_type, -1, context, &err_msg))
    {
        spdlog::error("启动模型任务失败: {}", err_msg.toUtf8().constData());
        return -1;
    }

    const int task_id = ensureTaskRecord(context);
    if (task_id < 0)
        return -1;

    startTask(task_id);
    return task_id;
}

bool ModelTaskController::stopModelTask(const QString &model_uuid, ModelTaskType task_type)
{
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr)
        return false;

    const int task_id = task_manager_->tasks()->findModelTask(model_uuid.trimmed(), task_type, false);
    if (task_id < 0)
        return false;
    return stopTask(task_id);
}

bool ModelTaskController::deleteModelTask(const QString &model_uuid, ModelTaskType task_type)
{
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr)
        return false;

    const int task_id = task_manager_->tasks()->findModelTask(model_uuid.trimmed(), task_type, false);
    if (task_id < 0)
        return false;
    return deleteTask(task_id);
}

bool ModelTaskController::buildContext(const QString &model_uuid, ModelTaskType task_type, int task_id,
                                       ModelTaskContext &context, QString *err_msg) const
{
    context = {};

    if (model_manager_ == nullptr)
        return setError(err_msg, QString("模型管理器为空"));
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr)
        return setError(err_msg, QString("任务管理器为空"));

    const QString trimmed_uuid = model_uuid.trimmed();
    if (trimmed_uuid.isEmpty())
        return setError(err_msg, QString("模型 uuid 为空"));
    if (!isKnownModelTask(task_type))
        return setError(err_msg, QString("任务类型无效"));

    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(trimmed_uuid);
    if (!record.isValid())
        return setError(err_msg, QString("模型不存在: %1").arg(trimmed_uuid));

    IModel *model = model_manager_->modelForUuid(trimmed_uuid);
    if (model == nullptr)
        return setError(err_msg, QString("无法创建模型实例: %1").arg(trimmed_uuid));

    const FrameworkDefinition framework = registeredFramework(method_, model->frameworkName());
    if (framework.name.isEmpty())
    {
        return setError(err_msg,
                        QString("框架未注册: %1").arg(model->frameworkName()));
    }

    context.task_id    = task_id;
    context.model_uuid = trimmed_uuid;
    context.model_name = record.name.trimmed();
    context.task_type  = task_type;
    context.model      = model;
    context.framework  = framework;
    if (context.model_name.isEmpty())
        return setError(err_msg, QString("模型名称为空: %1").arg(trimmed_uuid));
    return true;
}

int ModelTaskController::ensureTaskRecord(const ModelTaskContext &context)
{
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr)
        return -1;

    const int existing_task_id
        = task_manager_->tasks()->findModelTask(context.model_uuid, context.task_type, false);
    if (existing_task_id >= 0)
    {
        owned_task_ids_.insert(existing_task_id);
        return existing_task_id;
    }

    const bool supports_pause = !context.framework.supportsExternalTask(context.task_type);
    const int  task_id        = task_manager_->addTask(context.model_uuid, context.model_name, context.task_type,
                                                       supports_pause);
    if (task_id >= 0)
        owned_task_ids_.insert(task_id);
    return task_id;
}

bool ModelTaskController::startTask(int task_id)
{
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr)
        return false;

    const TaskTableModel::TaskSnapshot task = task_manager_->tasks()->taskSnapshotForId(task_id);
    if (!task.isValid())
        return false;

    if (task.status == TaskTableModel::Running)
        return true;
    if (task.status == TaskTableModel::Stopping)
        return false;
    if (!task.can_start)
        return false;

    ModelTaskContext context;
    QString          err_msg;
    if (!buildContext(task.model_uuid, task.task_type, task_id, context, &err_msg))
    {
        failTask(task_id, err_msg);
        return false;
    }

    if (!task_manager_->startTask(task_id))
        return false;

    if (context.framework.supportsExternalTask(context.task_type) && !startExternalTask(context))
        return false;

    return true;
}

bool ModelTaskController::stopTask(int task_id)
{
    if (task_manager_ == nullptr)
        return false;

    const bool had_external_process
        = external_task_runner_ != nullptr && external_task_runner_->hasRunningTask(task_id);
    const bool stop_requested = task_manager_->stopTask(task_id);
    if (stop_requested && !had_external_process)
        task_manager_->markTaskStopped(task_id);
    return stop_requested;
}

bool ModelTaskController::deleteTask(int task_id)
{
    const bool deleted_from_table = task_manager_ != nullptr && task_manager_->deleteTask(task_id);
    if (external_task_runner_ != nullptr)
    {
        const bool deleted_from_runner = external_task_runner_->deleteTask(task_id);
        if (deleted_from_table)
            owned_task_ids_.erase(task_id);
        return deleted_from_runner && deleted_from_table;
    }
    if (deleted_from_table)
        owned_task_ids_.erase(task_id);
    return deleted_from_table;
}

int ModelTaskController::addOwnedTask(const QString &task_uuid, const QString &task_name, ModelTaskType task_type,
                                      bool supports_pause)
{
    if (task_manager_ == nullptr)
        return -1;
    const int task_id = task_manager_->addTask(task_uuid, task_name, task_type, supports_pause);
    if (task_id >= 0)
        owned_task_ids_.insert(task_id);
    return task_id;
}

bool ModelTaskController::startPreparedExternalTask(int task_id, const ExternalProcessSpec &process_spec,
                                                    QString *err_msg)
{
    if (task_manager_ == nullptr || external_task_runner_ == nullptr)
        return setError(err_msg, QString("任务运行器未初始化"));
    if (task_manager_->tasks() == nullptr)
        return setError(err_msg, QString("任务表未初始化"));

    const TaskTableModel::TaskSnapshot task = task_manager_->tasks()->taskSnapshotForId(task_id);
    if (!task.isValid())
        return setError(err_msg, QString("任务不存在: %1").arg(task_id));
    if (task.status != TaskTableModel::Running && !task_manager_->startTask(task_id))
        return setError(err_msg, QString("任务无法启动: %1").arg(task_id));

    QString start_err;
    if (!external_task_runner_->start(process_spec, &start_err))
    {
        task_manager_->failTask(task_id);
        return setError(err_msg, start_err);
    }
    return true;
}

bool ModelTaskController::startExternalTask(const ModelTaskContext &context)
{
    if (task_manager_ == nullptr || external_task_runner_ == nullptr)
        return false;

    QString server_err;
    if (!task_manager_->ensureTaskServer(&server_err))
    {
        failTask(context.task_id, QString("任务通信服务启动失败: %1").arg(server_err));
        return false;
    }

    ModelTaskContext request = context;
    request.task_server_host = task_manager_->taskServerHost();
    request.task_server_port = task_manager_->taskServerPort();

    ExternalProcessSpec               process_spec;
    QString                           err_msg;
    const ModelTaskPreparationService preparation(method_, project_dir_, data_manager_);
    if (!preparation.prepare(request, process_spec, &err_msg))
    {
        failTask(context.task_id, err_msg);
        return false;
    }

    if (!external_task_runner_->start(process_spec, &err_msg))
    {
        failTask(context.task_id, err_msg);
        return false;
    }
    return true;
}

bool ModelTaskController::startFewShotLearning(const FewShotLearningRequest &request, QString *err_msg)
{
    if (few_shot_learning_running_ || few_shot_context_ != nullptr)
        return setError(err_msg, QString("已有小样本学习任务正在运行"));
    if (task_manager_ == nullptr || external_task_runner_ == nullptr)
        return setError(err_msg, QString("任务管理器未初始化"));

    setFewShotLearningLastError({});
    few_shot_stop_requested_ = false;

    FewShotLearningRunContext context;
    const FewShotLearningTaskService service(method_, project_dir_, data_manager_);
    if (!service.prepare(request, context, err_msg))
        return false;

    QString server_err;
    if (!task_manager_->ensureTaskServer(&server_err))
        return setError(err_msg, QString("任务通信服务启动失败: %1").arg(server_err));

    if (context.task_uuid.trimmed().isEmpty())
        return setError(err_msg, QString("小样本学习任务 uuid 为空"));
    context.task_host = task_manager_->taskServerHost();
    context.task_port = task_manager_->taskServerPort();
    context.train_task_id =
        addOwnedTask(context.task_uuid, FewShotLearningTaskService::trainTaskName(), ModelTaskType::Train, false);
    context.predict_task_id =
        addOwnedTask(context.task_uuid, FewShotLearningTaskService::predictTaskName(), ModelTaskType::Test, false);
    if (context.requires_box_to_mask)
    {
        context.box_to_mask_task_id = addOwnedTask(context.task_uuid, FewShotLearningTaskService::boxToMaskTaskName(),
                                                   ModelTaskType::BoxToMask, false);
    }
    if (context.train_task_id < 0 || context.predict_task_id < 0
        || (context.requires_box_to_mask && context.box_to_mask_task_id < 0))
    {
        return setError(err_msg, QString("创建小样本学习任务失败"));
    }

    few_shot_context_                    = std::make_unique<FewShotLearningRunContext>(std::move(context));
    current_few_shot_prepare_split_index_ = 0;
    current_few_shot_import_index_        = 0;

    QString start_err;
    bool    started = false;
    if (few_shot_context_->requires_box_to_mask)
    {
        few_shot_stage_ = FewShotLearningStage::PreparingMask;
        started         = startFewShotBoxToMask(0, &start_err);
    }
    else
    {
        few_shot_stage_ = FewShotLearningStage::Training;
        started         = startFewShotTraining(&start_err);
    }

    if (!started)
    {
        failFewShotLearning(start_err);
        return setError(err_msg, start_err);
    }

    setFewShotLearningRunning(true);
    return true;
}

bool ModelTaskController::stopFewShotLearning()
{
    if (few_shot_context_ == nullptr)
        return false;

    few_shot_stop_requested_ = true;
    markFewShotStoppedTasksForCurrentStage();
    if (few_shot_stage_ == FewShotLearningStage::Importing)
    {
        finishFewShotLearning(false, QString("小样本学习任务已停止"));
        return true;
    }
    return true;
}

bool ModelTaskController::isFewShotLearningRunning() const
{
    return few_shot_learning_running_;
}

QString ModelTaskController::fewShotLearningLastError() const
{
    return few_shot_last_error_;
}

bool ModelTaskController::startFewShotBoxToMask(int split_index, QString *err_msg)
{
    if (few_shot_context_ == nullptr)
        return setError(err_msg, QString("小样本学习上下文为空"));

    ExternalProcessSpec process_spec;
    const FewShotLearningTaskService service(method_, project_dir_, data_manager_);
    if (!service.buildBoxToMaskSpec(*few_shot_context_, split_index, process_spec, err_msg))
        return false;
    return startPreparedExternalTask(few_shot_context_->box_to_mask_task_id, process_spec, err_msg);
}

bool ModelTaskController::startFewShotTraining(QString *err_msg)
{
    if (few_shot_context_ == nullptr)
        return setError(err_msg, QString("小样本学习上下文为空"));

    ExternalProcessSpec process_spec;
    const FewShotLearningTaskService service(method_, project_dir_, data_manager_);
    if (!service.buildTrainingSpec(*few_shot_context_, process_spec, err_msg))
        return false;
    return startPreparedExternalTask(few_shot_context_->train_task_id, process_spec, err_msg);
}

bool ModelTaskController::startFewShotPrediction(QString *err_msg)
{
    if (few_shot_context_ == nullptr)
        return setError(err_msg, QString("小样本学习上下文为空"));

    ExternalProcessSpec process_spec;
    const FewShotLearningTaskService service(method_, project_dir_, data_manager_);
    if (!service.buildPredictionSpec(*few_shot_context_, process_spec, err_msg))
        return false;
    return startPreparedExternalTask(few_shot_context_->predict_task_id, process_spec, err_msg);
}

bool ModelTaskController::taskBelongsToFewShotLearning(int task_id) const
{
    if (few_shot_context_ == nullptr)
        return false;
    return task_id == few_shot_context_->box_to_mask_task_id || task_id == few_shot_context_->train_task_id
        || task_id == few_shot_context_->predict_task_id;
}

bool ModelTaskController::taskBelongsToCurrentModelManager(int task_id) const
{
    if (owned_task_ids_.find(task_id) != owned_task_ids_.end())
        return true;

    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr || model_manager_ == nullptr)
        return false;

    const TaskTableModel::TaskSnapshot task = task_manager_->tasks()->taskSnapshotForId(task_id);
    if (!task.isValid())
        return false;

    const QString model_uuid = task.model_uuid.trimmed();
    return !model_uuid.isEmpty() && model_manager_->modelRecordViewForUuid(model_uuid).isValid();
}

void ModelTaskController::failTask(int task_id, const QString &message) const
{
    if (!message.isEmpty())
        spdlog::error("模型任务 {} 失败: {}", task_id, message.toUtf8().constData());
    if (task_manager_ != nullptr)
        task_manager_->failTask(task_id);
}

void ModelTaskController::handleFewShotExternalTaskFinished(int task_id, int exit_code, bool normal_exit,
                                                            bool stop_requested)
{
    if (few_shot_context_ == nullptr || task_manager_ == nullptr)
        return;

    const bool stopped = stop_requested || few_shot_stop_requested_ || (normal_exit && exit_code == 2);
    const bool success = normal_exit && exit_code == 0 && !stopped;
    if (stopped)
    {
        markFewShotStoppedTasksForCurrentStage();
        finishFewShotLearning(false, QString("小样本学习任务已停止"));
        return;
    }
    if (!success)
    {
        failFewShotLearning(QString("小样本学习进程异常退出: %1").arg(exit_code));
        return;
    }

    if (few_shot_stage_ == FewShotLearningStage::PreparingMask
        && task_id == few_shot_context_->box_to_mask_task_id)
    {
        ++current_few_shot_prepare_split_index_;
        const FewShotLearningTaskService service(method_, project_dir_, data_manager_);
        if (current_few_shot_prepare_split_index_ < service.boxToMaskSplitCount(*few_shot_context_))
        {
            QString err_msg;
            if (!startFewShotBoxToMask(current_few_shot_prepare_split_index_, &err_msg))
                failFewShotLearning(err_msg);
            return;
        }

        task_manager_->finishTask(few_shot_context_->box_to_mask_task_id);
        few_shot_stage_ = FewShotLearningStage::Training;
        QString err_msg;
        if (!startFewShotTraining(&err_msg))
            failFewShotLearning(err_msg);
        return;
    }

    if (few_shot_stage_ == FewShotLearningStage::Training && task_id == few_shot_context_->train_task_id)
    {
        QString checkpoint_err;
        if (!storeFewShotCheckpoint(&checkpoint_err))
        {
            failFewShotLearning(checkpoint_err);
            return;
        }

        task_manager_->finishTask(few_shot_context_->train_task_id);
        few_shot_stage_ = FewShotLearningStage::Predicting;
        QString err_msg;
        if (!startFewShotPrediction(&err_msg))
            failFewShotLearning(err_msg);
        return;
    }

    if (few_shot_stage_ == FewShotLearningStage::Predicting && task_id == few_shot_context_->predict_task_id)
    {
        task_manager_->finishTask(few_shot_context_->predict_task_id);
        startFewShotPredictionImports();
    }
}

void ModelTaskController::startFewShotPredictionImports()
{
    if (few_shot_context_ == nullptr || data_manager_ == nullptr || few_shot_context_->import_targets.empty())
    {
        finishFewShotLearning(true);
        return;
    }

    few_shot_stage_                 = FewShotLearningStage::Importing;
    current_few_shot_import_index_  = 0;
    few_shot_import_finished_connection_ = data_manager_->connectImportFinished(
        this, [this](bool success, const QString &message)
        { handleFewShotPredictionImportFinished(success, message); });
    startNextFewShotPredictionImport();
}

void ModelTaskController::startNextFewShotPredictionImport()
{
    if (few_shot_context_ == nullptr || data_manager_ == nullptr)
    {
        finishFewShotLearning(false, QString("数据管理器未初始化"));
        return;
    }

    if (current_few_shot_import_index_ >= static_cast<int>(few_shot_context_->import_targets.size()))
    {
        finishFewShotLearning(true);
        return;
    }

    const FewShotPredictionImportTarget &target =
        few_shot_context_->import_targets.at(static_cast<size_t>(current_few_shot_import_index_));
    data_manager_->importMaskData(target.dataset_id, target.manifest_path, few_shot_context_->output_dir);
}

void ModelTaskController::handleFewShotPredictionImportFinished(bool success, const QString &message)
{
    if (few_shot_stage_ != FewShotLearningStage::Importing)
        return;

    if (!success)
    {
        finishFewShotLearning(false, message);
        return;
    }

    ++current_few_shot_import_index_;
    startNextFewShotPredictionImport();
}

void ModelTaskController::markFewShotStoppedTasksForCurrentStage()
{
    if (few_shot_context_ == nullptr || task_manager_ == nullptr || task_manager_->tasks() == nullptr)
        return;

    auto stop_or_mark = [this](int task_id)
    {
        if (task_id < 0)
            return;
        const TaskTableModel::TaskSnapshot task = task_manager_->tasks()->taskSnapshotForId(task_id);
        if (!task.isValid())
            return;
        if (task.can_stop || task.status == TaskTableModel::Running || task.status == TaskTableModel::Paused)
            task_manager_->stopTask(task_id);
        else if (task.status == TaskTableModel::Pending || task.status == TaskTableModel::Stopping)
            task_manager_->markTaskStopped(task_id);
    };

    if (few_shot_stage_ == FewShotLearningStage::PreparingMask)
    {
        stop_or_mark(few_shot_context_->box_to_mask_task_id);
        stop_or_mark(few_shot_context_->train_task_id);
        stop_or_mark(few_shot_context_->predict_task_id);
        return;
    }
    if (few_shot_stage_ == FewShotLearningStage::Training)
    {
        stop_or_mark(few_shot_context_->train_task_id);
        stop_or_mark(few_shot_context_->predict_task_id);
        return;
    }
    if (few_shot_stage_ == FewShotLearningStage::Predicting)
    {
        stop_or_mark(few_shot_context_->predict_task_id);
    }
}

bool ModelTaskController::storeFewShotCheckpoint(QString *err_msg) const
{
    if (few_shot_context_ == nullptr)
        return setError(err_msg, QString("小样本学习上下文为空"));

    const QString source_path = few_shot_context_->training_checkpoint_path;
    const QString target_path = few_shot_context_->checkpoint_path;
    if (!QFileInfo::exists(source_path))
        return setError(err_msg, QString("未找到训练模型: %1").arg(source_path));
    if (target_path.trimmed().isEmpty())
        return setError(err_msg, QString("小样本模型权重保存路径为空"));

    const QString target_dir = QFileInfo(target_path).absoluteDir().absolutePath();
    if (!QDir().mkpath(target_dir))
        return setError(err_msg, QString("创建小样本模型权重目录失败: %1").arg(target_dir));
    if (QFileInfo::exists(target_path) && !QFile::remove(target_path))
        return setError(err_msg, QString("覆盖小样本模型权重失败: %1").arg(target_path));
    if (!QFile::copy(source_path, target_path))
        return setError(err_msg, QString("保存小样本模型权重失败: %1").arg(target_path));
    return true;
}

void ModelTaskController::failFewShotLearning(const QString &message)
{
    if (few_shot_context_ != nullptr && task_manager_ != nullptr)
    {
        if (few_shot_stage_ == FewShotLearningStage::PreparingMask)
            task_manager_->failTask(few_shot_context_->box_to_mask_task_id);
        if (few_shot_stage_ == FewShotLearningStage::Training)
            task_manager_->failTask(few_shot_context_->train_task_id);
        if (few_shot_stage_ == FewShotLearningStage::PreparingMask
            || few_shot_stage_ == FewShotLearningStage::Training
            || few_shot_stage_ == FewShotLearningStage::Predicting)
        {
            task_manager_->failTask(few_shot_context_->predict_task_id);
        }
    }
    finishFewShotLearning(false, message);
}

void ModelTaskController::finishFewShotLearning(bool success, const QString &message)
{
    if (data_manager_ != nullptr && few_shot_import_finished_connection_)
        data_manager_->disconnectImportFinished(few_shot_import_finished_connection_);
    few_shot_import_finished_connection_ = {};

    if (!success && !message.isEmpty())
    {
        setFewShotLearningLastError(message);
        spdlog::error("小样本学习任务失败: {}", message.toUtf8().constData());
    }

    if (few_shot_context_ != nullptr)
    {
        owned_task_ids_.erase(few_shot_context_->box_to_mask_task_id);
        owned_task_ids_.erase(few_shot_context_->train_task_id);
        owned_task_ids_.erase(few_shot_context_->predict_task_id);
    }

    few_shot_context_.reset();
    few_shot_stage_                       = FewShotLearningStage::Idle;
    current_few_shot_prepare_split_index_ = 0;
    current_few_shot_import_index_        = 0;
    few_shot_stop_requested_              = false;
    setFewShotLearningRunning(false);
    emit fewShotLearningFinished(success, message);
}

void ModelTaskController::setFewShotLearningRunning(bool running)
{
    if (few_shot_learning_running_ == running)
        return;
    few_shot_learning_running_ = running;
    emit fewShotLearningRunningChanged(running);
}

void ModelTaskController::setFewShotLearningLastError(const QString &message)
{
    if (few_shot_last_error_ == message)
        return;
    few_shot_last_error_ = message;
    emit fewShotLearningLastErrorChanged(message);
}

void ModelTaskController::handleTaskStopRequested(int task_id)
{
    if (external_task_runner_ == nullptr || !taskBelongsToCurrentModelManager(task_id))
        return;
    if (taskBelongsToFewShotLearning(task_id))
        few_shot_stop_requested_ = true;
    external_task_runner_->stop(task_id);
}

void ModelTaskController::handleExternalTaskFinished(int task_id, int exit_code, bool normal_exit,
                                                     bool stop_requested)
{
    if (taskBelongsToFewShotLearning(task_id))
    {
        handleFewShotExternalTaskFinished(task_id, exit_code, normal_exit, stop_requested);
        return;
    }

    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr || !taskBelongsToCurrentModelManager(task_id))
        return;

    if (stop_requested || (normal_exit && exit_code == 2))
        task_manager_->markTaskStopped(task_id);
    else if (normal_exit && exit_code == 0)
        task_manager_->finishTask(task_id);
    else
        task_manager_->failTask(task_id);
}

} // namespace dltool::model
