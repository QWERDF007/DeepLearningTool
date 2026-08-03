#include "model/ModelTaskController.h"

#include "common/Utils.h"
#include "common/YamlUtils.h"
#include "core/CoreDef.h"
#include "data/DataManager.h"
#include "data/DatasetExportSource.h"
#include "data/DataOperationWorkflow.h"
#include "model/ExternalModelTaskRunner.h"
#include "model/IModel.h"
#include "model/IModelConfig.h"
#include "model/IParams.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelManager.h"
#include "model/ModelStorageService.h"
#include "model/TaskManager.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <memory>
#include <exception>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThreadPool>
#include <algorithm>
#include <utility>

namespace dltool::model {
using common::setError;
using common::cleanPath;

namespace {

QString taskManagerStatusName(const TaskManager::TaskStatus status)
{
    switch (status)
    {
    case TaskManager::Pending:
        return QStringLiteral("pending");
    case TaskManager::Preparing:
        return QStringLiteral("preparing");
    case TaskManager::Running:
        return QStringLiteral("running");
    case TaskManager::Paused:
        return QStringLiteral("paused");
    case TaskManager::Stopping:
        return QStringLiteral("stopping");
    case TaskManager::Stopped:
        return QStringLiteral("stopped");
    case TaskManager::Finished:
        return QStringLiteral("finished");
    case TaskManager::Failed:
        return QStringLiteral("failed");
    default:
        return {};
    }
}

bool isFewShotFramework(const QString &framework_name)
{
    return framework_name.compare(QString("FS-SAM2"), Qt::CaseInsensitive) == 0;
}

} // namespace

ModelTaskController::ModelTaskController(const int method, QString project_dir, ModelManager *model_manager,
                                         dltool::data::DataManager *data_manager, TaskManager *task_manager,
                                         QObject *parent)
    : QObject(parent)
    , method_(method)
    , project_dir_(std::move(project_dir))
    , model_manager_(model_manager)
    , data_manager_(data_manager)
    , task_manager_(task_manager)
    , external_task_runner_(std::make_unique<ExternalModelTaskRunner>(this))
    , test_task_repository_(project_dir_)
{
    connect(external_task_runner_.get(), &ExternalModelTaskRunner::taskStarted, this,
            &ModelTaskController::handleExternalTaskStarted);
    connect(external_task_runner_.get(), &ExternalModelTaskRunner::taskFinished, this,
            &ModelTaskController::handleExternalTaskFinished);
    connect(external_task_runner_.get(), &ExternalModelTaskRunner::taskStartFailed, this,
            &ModelTaskController::handleExternalTaskStartFailed);
    if (task_manager_ != nullptr)
    {
        connect(task_manager_, &TaskManager::taskStartRequested, this,
                &ModelTaskController::handleTaskStartRequested);
        connect(task_manager_, &TaskManager::taskStopRequested, this, &ModelTaskController::handleTaskStopRequested);
        connect(task_manager_, &TaskManager::taskMessageReceived, this, &ModelTaskController::handleTaskMessage);
    }
}

ModelTaskController::~ModelTaskController()
{
    shutdown();
}

void ModelTaskController::shutdown()
{
    if (task_manager_ != nullptr)
    {
        const int count = task_manager_->rowCount();
        for (int row = 0; row < count; ++row)
        {
            const QModelIndex index = task_manager_->index(row, 0);
            const int task_id = task_manager_->data(index, TaskManager::TaskIdRole).toInt();
            const TaskManager::Task *task = task_manager_->findTask(task_id);
            if (task == nullptr || model_manager_ == nullptr
                || !model_manager_->modelRecordViewForUuid(task->model_uuid).isValid()
                || TaskManager::isTerminal(task->status))
                continue;
            if (external_task_runner_ != nullptr && external_task_runner_->hasRunningTask(task_id))
                external_task_runner_->stop(task_id);
            else
                task_manager_->markTaskStopped(task_id);
        }
    }
    // Let background preparation/data-export callbacks settle before project
    // teardown releases their owner.
    QThreadPool::globalInstance()->waitForDone(5000);
    if (external_task_runner_ != nullptr)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000)
        {
            bool running = false;
            if (task_manager_ != nullptr)
            {
                for (int row = 0; row < task_manager_->rowCount(); ++row)
                {
                    const int task_id = task_manager_->data(task_manager_->index(row, 0), TaskManager::TaskIdRole).toInt();
                    if (external_task_runner_->hasRunningTask(task_id))
                    {
                        running = true;
                        break;
                    }
                }
            }
            if (!running)
                break;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        }
    }
}

int ModelTaskController::addModelTask(const QString &model_uuid, const ModelTaskType task_type)
{
    QString error;
    const int task_id = ensureTaskRecord(model_uuid, task_type, {}, {}, &error);
    if (task_id < 0)
        spdlog::error("添加模型任务失败: {}", error.toUtf8().constData());
    return task_id;
}

int ModelTaskController::startModelTask(const QString &model_uuid, const ModelTaskType task_type)
{
    QString error;
    const int task_id = ensureTaskRecord(model_uuid, task_type, {}, {}, &error);
    if (task_id < 0)
    {
        spdlog::error("启动模型任务失败: {}", error.toUtf8().constData());
        return -1;
    }
    return task_manager_->startTask(task_id) ? task_id : -1;
}

int ModelTaskController::startModelTestTask(const QString &model_uuid, const QString &test_task_uuid)
{
    QString error;
    const int task_id = ensureTaskRecord(model_uuid, ModelTaskType::Test, test_task_uuid, {}, &error);
    if (task_id < 0)
    {
        spdlog::error("启动测试任务失败: {}", error.toUtf8().constData());
        return -1;
    }
    return task_manager_ != nullptr && task_manager_->startTask(task_id) ? task_id : -1;
}

bool ModelTaskController::stopModelTask(const QString &model_uuid, const ModelTaskType task_type)
{
    if (task_manager_ == nullptr)
        return false;

    const int task_id = task_manager_->findModelTask(model_uuid.trimmed(), task_type, false);
    return task_id >= 0 && stopTask(task_id);
}

bool ModelTaskController::stopModelTestTask(const QString &model_uuid, const QString &test_task_uuid)
{
    if (task_manager_ == nullptr)
        return false;
    const int task_id = task_manager_->findModelTask(model_uuid.trimmed(), ModelTaskType::Test,
                                                     test_task_uuid.trimmed(), false);
    return task_id >= 0 && stopTask(task_id);
}

bool ModelTaskController::deleteModelTask(const QString &model_uuid, const ModelTaskType task_type)
{
    if (task_manager_ == nullptr)
        return false;

    const int task_id = task_manager_->findModelTask(model_uuid.trimmed(), task_type, false);
    return task_id >= 0 && deleteTask(task_id);
}

int ModelTaskController::ensureTaskRecord(const QString &model_uuid, const ModelTaskType task_type,
                                          const QString &scope_uuid, const QString &scope_name, QString *err_msg)
{
    if (model_manager_ == nullptr)
    {
        setError(err_msg, QString("模型管理器为空"));
        return -1;
    }
    if (task_manager_ == nullptr)
    {
        setError(err_msg, QString("任务管理器为空"));
        return -1;
    }

    const QString uuid = model_uuid.trimmed();
    if (uuid.isEmpty())
    {
        setError(err_msg, QString("模型 uuid 为空"));
        return -1;
    }
    if (!isKnownModelTask(task_type))
    {
        setError(err_msg, QString("任务类型无效"));
        return -1;
    }

    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(uuid);
    if (!record.isValid() || record.name.trimmed().isEmpty())
    {
        setError(err_msg, QString("模型不存在: %1").arg(uuid));
        return -1;
    }

    const FrameworkDefinition framework = registeredFramework(method_, record.framework_name);
    if (framework.name.isEmpty())
    {
        setError(err_msg, QString("框架未注册: %1").arg(record.framework_name));
        return -1;
    }

    QString resolved_scope = scope_uuid.trimmed();
    QString resolved_scope_name = scope_name.trimmed();
    if (isTrainModelTask(task_type))
        resolved_scope = QStringLiteral("train");
    if (isTestModelTask(task_type) && resolved_scope.isEmpty() && !isFewShotFramework(record.framework_name))
    {
        setError(err_msg, QString("普通测试任务必须绑定测试任务 UUID"));
        return -1;
    }
    ModelTestTaskDefinition resolved_definition;
    bool has_resolved_definition = false;
    if (isTestModelTask(task_type) && !resolved_scope.isEmpty())
    {
        if (!test_task_repository_.loadTask(record.name, resolved_scope, resolved_definition, err_msg))
            return -1;
        resolved_scope_name = resolved_definition.name;
        has_resolved_definition = true;
    }

    int task_id = task_manager_->findModelTask(uuid, task_type, resolved_scope, false);
    if (task_id < 0)
    {
        task_id = task_manager_->addTask(uuid, record.name, task_type, resolved_scope, resolved_scope_name,
                                         !framework.supportsExternalTask(task_type));
    }
    if (task_id < 0)
        return -1;

    const ModelStorageService storage(project_dir_);
    QString config_path;
    QString log_path;
    if (isTrainModelTask(task_type))
    {
        config_path = storage.trainConfigPath(record.name);
        log_path = storage.trainLogPath(record.name);
    }
    else if (has_resolved_definition)
    {
        config_path = storage.testTaskConfigPath(record.name, resolved_definition.directory_name);
        log_path = storage.testTaskLogPath(record.name, resolved_definition.uuid);
    }
    task_manager_->setTaskPaths(task_id, config_path, log_path);
    return task_id;
}

bool ModelTaskController::prepareTask(const int task_id)
{
    if (task_manager_ == nullptr || model_manager_ == nullptr)
        return false;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr || task->status != TaskManager::Preparing)
        return false;

    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(task->model_uuid);
    const FrameworkDefinition framework = registeredFramework(method_, record.framework_name);
    if (record.name.isEmpty() || framework.name.isEmpty())
    {
        failTask(task_id, QString("模型或框架不存在"));
        return false;
    }

    if (!framework.supportsExternalTask(task->type))
    {
        if (!task_manager_->markTaskRunning(task_id))
        {
            failTask(task_id, QString("内部模型任务无法进入运行状态"));
            return false;
        }
        syncTaskModelState(task_id);
        touchTaskModelModifiedTime(task_id);
        return true;
    }

    QString server_error;
    if (!task_manager_->ensureTaskServer(&server_error))
    {
        failTask(task_id, QString("任务通信服务启动失败: %1").arg(server_error));
        return false;
    }

    ModelTaskRequest request;
    QString          request_error;
    if (!buildTaskRequest(task_id, request, &request_error))
    {
        failTask(task_id, request_error);
        return false;
    }
    const auto process_spec = std::make_shared<ExternalProcessSpec>();
    const auto request_ptr = std::make_shared<ModelTaskRequest>(std::move(request));

    dltool::data::DataOperationWorkflow::Options options;
    options.title            = QString("准备模型任务");
    options.start_message    = QString("准备模型任务: %1").arg(modelTaskDisplayName(request_ptr->task_type));
    options.initial_progress = 5;

    const int method = method_;
    const QString project_dir = project_dir_;
    const auto prepare = [method, project_dir, request_ptr, process_spec](
                             const dltool::data::DatasetExportSource *dataset_source,
                             dltool::data::DataOperationWorkflow::Result &result)
    {
        ModelTaskRequest &request = *request_ptr;
        QString error;
        if (!prepareModelTask(method, project_dir, request, dataset_source, *process_spec, &error))
        {
            result.error = error;
            return;
        }
        result.success = true;
    };
    const auto completion = [this, task_id, process_spec](
                                const dltool::data::DataOperationWorkflow::Result &result)
    {
        handlePreparedTask(task_id, process_spec, result.success, result.error);
    };

    if (!describeModelTask(request_ptr->task_type).requires_dataset_export)
    {
        dltool::data::DataOperationWorkflow::start(
            this, std::move(options),
            [prepare](dltool::data::DataOperationWorkflow::Result &result) { prepare(nullptr, result); }, completion);
    }
    else
    {
        if (data_manager_ == nullptr)
        {
            failTask(task_id, QString("数据管理器为空"));
            return false;
        }

        dltool::data::DatasetExportRequest export_request;
        export_request.dataset_ids = selectedDatasetIds(request_ptr->selections);
        data_manager_->runDatasetExportAsync(
            this, std::move(export_request), std::move(options),
            [prepare](const dltool::data::DatasetExportSource &source, dltool::data::DataOperationWorkflow::Result &result)
            { prepare(&source, result); },
            completion);
    }

    spdlog::info("模型任务进入后台准备, task_id: {}", task_id);
    return true;
}

bool ModelTaskController::stopTask(const int task_id)
{
    return task_manager_ != nullptr && task_manager_->stopTask(task_id);
}

bool ModelTaskController::deleteTask(const int task_id)
{
    const bool deleted = task_manager_ != nullptr && task_manager_->deleteTask(task_id);
    if (external_task_runner_ != nullptr)
        external_task_runner_->deleteTask(task_id);
    return deleted;
}

bool ModelTaskController::buildTaskRequest(const int task_id, ModelTaskRequest &request, QString *err_msg) const
{
    request = {};
    if (task_manager_ == nullptr || model_manager_ == nullptr)
        return setError(err_msg, QString("任务控制器未初始化"));

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr)
        return setError(err_msg, QString("任务不存在"));

    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(task->model_uuid);
    if (!record.isValid())
        return setError(err_msg, QString("模型不存在: %1").arg(task->model_uuid));

    IModel *model = model_manager_->modelForUuid(task->model_uuid);
    if (model == nullptr)
        return setError(err_msg, QString("无法创建模型实例: %1").arg(task->model_uuid));

    const FrameworkDefinition framework = registeredFramework(method_, record.framework_name);
    if (framework.name.isEmpty())
        return setError(err_msg, QString("框架未注册: %1").arg(record.framework_name));

    request.task_id          = task->id;
    request.task_type        = task->type;
    request.scope_uuid       = task->scope_uuid;
    request.scope_name       = task->scope_name;
    request.evaluation_method = evaluation::fromProjectMethod(method_);
    request.framework        = framework;
    request.task_server_host = task_manager_->taskServerHost();
    request.task_server_port = task_manager_->taskServerPort();
    request.selections       = modelDatasetSelections(model);
    request.model_config.model_uuid         = record.uuid;
    request.model_config.model_name         = record.name;
    request.model_config.framework_name     = record.framework_name;
    request.model_config.method              = evaluation::methodKey(request.evaluation_method);
    request.model_config.model_architecture = record.model_architecture;
    request.model_config.scope_uuid         = task->scope_uuid;
    request.model_config.scope_name         = task->scope_name;
    request.model_config.task_directory     = task->scope_name;

    if (const IModelConfig *config = model->config(); config != nullptr)
    {
        if (const ITrainParams *params = config->trainParams(); params != nullptr)
            request.model_config.train_params = params->valuesMap();
        if (const ITestParams *params = config->testParams(); params != nullptr)
            request.model_config.test_params = params->valuesMap();
    }

    if (isTestModelTask(task->type) && !task->scope_uuid.trimmed().isEmpty())
    {
        ModelTestTaskDefinition definition;
        QString error;
        if (!test_task_repository_.loadTask(record.name, task->scope_uuid, definition, &error))
            return setError(err_msg, error);
        request.selections = {};
        request.selections.test = definition.dataset_selection;
        request.model_config.test_params = definition.test_params;
        request.model_config.task_definition_test_params = definition.test_params;
        request.scope_name = definition.name;
        request.model_config.scope_name = definition.name;
        request.model_config.task_directory = definition.directory_name;
        request.model_config.test_dataset_selection = definition.dataset_selection;
        request.model_config.created_at = definition.created_at;
        request.model_config.modified_at = definition.modified_at;
    }
    return true;
}

void ModelTaskController::handlePreparedTask(const int task_id, const std::shared_ptr<ExternalProcessSpec> &process_spec,
                                             const bool success, const QString &error)
{
    if (task_manager_ == nullptr)
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    // 停止或删除发生在后台准备期间时，任务已不再是 Preparing，完成回调只需丢弃。
    if (task == nullptr || task->status != TaskManager::Preparing)
        return;

    if (!success)
    {
        failTask(task_id, error.isEmpty() ? QString("准备模型任务失败") : error);
        return;
    }

    QString start_error;
    if (process_spec == nullptr || external_task_runner_ == nullptr
        || !external_task_runner_->start(*process_spec, &start_error))
    {
        failTask(task_id, start_error.isEmpty() ? QString("启动外部模型任务失败") : start_error);
    }
}

bool ModelTaskController::taskBelongsToCurrentModelManager(const int task_id) const
{
    if (task_manager_ == nullptr || model_manager_ == nullptr)
        return false;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    return task != nullptr && model_manager_->modelRecordViewForUuid(task->model_uuid).isValid();
}

void ModelTaskController::failTask(const int task_id, const QString &message) const
{
    if (task_manager_ == nullptr || !task_manager_->failTask(task_id))
        return;

    syncTaskModelState(task_id);

    if (!message.isEmpty())
    {
        spdlog::error("模型任务 {} 失败: {}", task_id, message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("模型任务 %1 失败").arg(task_id), message);
    }
}

void ModelTaskController::touchTaskModelModifiedTime(const int task_id) const
{
    if (task_manager_ == nullptr || model_manager_ == nullptr)
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr)
        return;

    QString error;
    if (!model_manager_->touchModelModifiedTime(task->model_uuid, &error))
    {
        spdlog::error("更新任务对应模型修改时间失败, task_id: {}, uuid: {}, 错误: {}", task_id,
                      task->model_uuid.toUtf8().constData(), error.toUtf8().constData());
    }
}

void ModelTaskController::syncTaskModelState(const int task_id) const
{
    if (task_manager_ == nullptr || model_manager_ == nullptr)
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr || (!isTrainModelTask(task->type) && !isTestModelTask(task->type)))
        return;

    const bool train_scope = isTrainModelTask(task->type);
    const bool legacy_few_shot_test = isTestModelTask(task->type) && task->scope_uuid.trimmed().isEmpty();
    const QString phase = train_scope ? QStringLiteral("train") : QStringLiteral("test_tasks");
    const QVariantMap current_model = model_manager_->modelRecordForUuid(task->model_uuid);
    const QVariantMap extra_data = current_model.value(QStringLiteral("extra_data")).toMap();
    QVariantMap test_tasks = extra_data.value(QStringLiteral("test_tasks")).toMap();
    QVariantMap section = train_scope ? extra_data.value(phase).toMap()
                                      : (legacy_few_shot_test ? extra_data.value(QStringLiteral("test")).toMap()
                                                              : test_tasks.value(task->scope_uuid).toMap());

    // TaskManager owns the overall task progress.  The model page previously
    // kept the last phase progress (for example, 90% after training), which
    // could differ from the 100% terminal value shown in TaskCenterWindow.
    section.insert(QStringLiteral("progress"), task->progress);

    if (TaskManager::isTerminal(task->status))
    {
        section.insert(QStringLiteral("started"), false);
        section.insert(QStringLiteral("status"), taskManagerStatusName(task->status));
    }
    else if (task->status == TaskManager::Running || task->status == TaskManager::Paused
             || task->status == TaskManager::Stopping)
    {
        section.insert(QStringLiteral("started"), true);
        section.insert(QStringLiteral("status"), taskManagerStatusName(task->status));
    }

    QString error;
    QVariantMap state_update;
    if (train_scope)
        state_update.insert(phase, section);
    else if (legacy_few_shot_test)
        state_update.insert(QStringLiteral("test"), section);
    else
    {
        test_tasks.insert(task->scope_uuid, section);
        state_update.insert(QStringLiteral("test_tasks"), test_tasks);
    }
    if (!model_manager_->updateModelExtraData(task->model_uuid, state_update, &error))
    {
        spdlog::error("同步模型任务状态失败, task_id: {}, uuid: {}, 错误: {}", task_id,
                      task->model_uuid.toUtf8().constData(), error.toUtf8().constData());
    }
}

void ModelTaskController::handleTaskStartRequested(const int task_id)
{
    if (taskBelongsToCurrentModelManager(task_id))
        prepareTask(task_id);
}

void ModelTaskController::handleTaskMessage(const TaskMessage &message)
{
    if (model_manager_ == nullptr || task_manager_ == nullptr || message.task_id < 0
        || message.type == TaskMessageType::Log || message.type == TaskMessageType::Command)
    {
        return;
    }

    const TaskManager::Task *task = task_manager_->findTask(message.task_id);
    if (task == nullptr || (!isTrainModelTask(task->type) && !isTestModelTask(task->type)))
        return;

    // The top-level model data is keyed by the software task type.  A train
    // runner may report validation/evaluation as phase "test", but that is
    // still part of the training task and must not update the separate Test
    // page's state.
    const bool train_scope = isTrainModelTask(task->type);
    const bool legacy_few_shot_test = isTestModelTask(task->type) && task->scope_uuid.trimmed().isEmpty();
    const QString phase = train_scope ? QStringLiteral("train") : QStringLiteral("test_tasks");

    QVariantMap updates;
    if (message.status == TaskProtocolStatus::Running || message.payload.contains(QStringLiteral("started")))
        updates.insert(QStringLiteral("started"), message.payload.value(QStringLiteral("started"), true).toBool());
    if (message.progress >= 0)
        updates.insert(QStringLiteral("progress"), message.progress);

    for (const QString &key : {QStringLiteral("epoch"), QStringLiteral("iter"), QStringLiteral("lr"),
                               QStringLiteral("loss"), QStringLiteral("elapsed"), QStringLiteral("eta")})
    {
        if (message.payload.contains(key))
            updates.insert(key, message.payload.value(key).toString());
    }
    if (message.payload.contains(QStringLiteral("metrics")))
        updates.insert(QStringLiteral("metrics"), message.payload.value(QStringLiteral("metrics")).toString());
    if (!message.message.isEmpty())
        updates.insert(QStringLiteral("message"), message.message);

    const QString status = taskProtocolStatusName(message.status);
    if (!status.isEmpty())
        updates.insert(QStringLiteral("status"), status);

    const bool terminal = message.status == TaskProtocolStatus::Stopped || message.status == TaskProtocolStatus::Finished
                       || message.status == TaskProtocolStatus::Failed || message.status == TaskProtocolStatus::Error;
    if (terminal)
    {
        // Some runners send the final status without a progress field (or
        // attach it to an internal evaluation phase), so explicitly close the
        // phase belonging to this software task here.
        updates.insert(QStringLiteral("started"), false);
        if (message.status == TaskProtocolStatus::Finished)
            updates.insert(QStringLiteral("progress"), 100);
        touchTaskModelModifiedTime(message.task_id);
    }

    const QVariantMap current_model = model_manager_->modelRecordForUuid(task->model_uuid);
    const QVariantMap extra_data = current_model.value(QStringLiteral("extra_data")).toMap();
    QVariantMap test_tasks = extra_data.value(QStringLiteral("test_tasks")).toMap();
    QVariantMap section = train_scope ? extra_data.value(phase).toMap()
                                      : (legacy_few_shot_test ? extra_data.value(QStringLiteral("test")).toMap()
                                                              : test_tasks.value(task->scope_uuid).toMap());
    for (auto it = updates.cbegin(); it != updates.cend(); ++it)
        section.insert(it.key(), it.value());

    // Keep the phase shown by ModelDelegate on the same overall progress as
    // TaskManager/TaskCenterWindow.  A training task can enter an internal
    // validation phase after its training progress reaches 90%, while the
    // task itself continues to 100%.
    const bool completed = message.status == TaskProtocolStatus::Finished;
    auto applyTaskState = [task, terminal, completed](QVariantMap &target) {
        target.insert(QStringLiteral("progress"), completed ? 100 : task->progress);
        if (terminal)
        {
            target.insert(QStringLiteral("started"), false);
            target.insert(QStringLiteral("status"), taskManagerStatusName(task->status));
        }
        else if (task->status == TaskManager::Running || task->status == TaskManager::Paused
                 || task->status == TaskManager::Stopping)
        {
            target.insert(QStringLiteral("started"), true);
            target.insert(QStringLiteral("status"), taskManagerStatusName(task->status));
        }
    };
    applyTaskState(section);

    QString error;
    QVariantMap state_update;
    if (train_scope)
        state_update.insert(phase, section);
    else if (legacy_few_shot_test)
        state_update.insert(QStringLiteral("test"), section);
    else
    {
        test_tasks.insert(task->scope_uuid, section);
        state_update.insert(QStringLiteral("test_tasks"), test_tasks);
    }
    if (!model_manager_->updateModelExtraData(task->model_uuid, state_update, &error))
    {
        spdlog::error("保存模型任务状态失败, task_id: {}, uuid: {}, 错误: {}", message.task_id,
                      task->model_uuid.toUtf8().constData(), error.toUtf8().constData());
    }
}

void ModelTaskController::handleTaskStopRequested(const int task_id)
{
    if (!taskBelongsToCurrentModelManager(task_id))
        return;

    if (external_task_runner_ != nullptr && external_task_runner_->hasRunningTask(task_id))
    {
        external_task_runner_->stop(task_id);
        return;
    }

    if (task_manager_ != nullptr)
        task_manager_->markTaskStopped(task_id);
    syncTaskModelState(task_id);
    touchTaskModelModifiedTime(task_id);
}

void ModelTaskController::handleExternalTaskStarted(const int task_id)
{
    if (task_manager_ == nullptr || !taskBelongsToCurrentModelManager(task_id))
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr)
        return;

    if (task->status == TaskManager::Preparing)
    {
        if (task_manager_->markTaskRunning(task_id))
        {
            syncTaskModelState(task_id);
            touchTaskModelModifiedTime(task_id);
        }
        return;
    }

    // 用户在 QProcess::Starting 阶段点击停止时，进程刚启动也必须继续收敛。
    if (task->status == TaskManager::Stopping && external_task_runner_ != nullptr)
        external_task_runner_->stop(task_id);
}

void ModelTaskController::handleExternalTaskStartFailed(const int task_id, const QString &error)
{
    if (task_manager_ == nullptr || !taskBelongsToCurrentModelManager(task_id))
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr)
        return;

    if (task->status == TaskManager::Stopping)
    {
        task_manager_->markTaskStopped(task_id);
        syncTaskModelState(task_id);
        touchTaskModelModifiedTime(task_id);
        return;
    }
    if (task->status == TaskManager::Preparing)
        failTask(task_id, error.isEmpty() ? QString("外部模型任务进程启动失败") : error);
}

void ModelTaskController::handleExternalTaskFinished(const int task_id, const int exit_code, const bool normal_exit,
                                                     const bool stop_requested)
{
    if (task_manager_ == nullptr || !taskBelongsToCurrentModelManager(task_id))
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr)
        return;
    if (TaskManager::isTerminal(task->status))
    {
        syncTaskModelState(task_id);
        return;
    }

    touchTaskModelModifiedTime(task_id);
    if (task->status == TaskManager::Stopping || stop_requested || (normal_exit && exit_code == 2))
    {
        task_manager_->markTaskStopped(task_id);
        syncTaskModelState(task_id);
        return;
    }
    if (normal_exit && exit_code == 0)
    {
        if (task->status == TaskManager::Preparing)
            task_manager_->markTaskRunning(task_id);
        task_manager_->updateTaskPhase(task_id, QStringLiteral("finished"));
        task_manager_->finishTask(task_id);
        syncTaskModelState(task_id);
        return;
    }

    task = task_manager_->findTask(task_id);
    if (task != nullptr && !TaskManager::isTerminal(task->status))
    {
        const QString name = modelTaskDisplayName(task->type);
        failTask(task_id, normal_exit ? QString("%1失败（退出码 %2），请查看模型日志。").arg(name).arg(exit_code)
                                      : QString("%1异常退出，请查看模型日志。").arg(name));
    }
}

} // namespace dltool::model
