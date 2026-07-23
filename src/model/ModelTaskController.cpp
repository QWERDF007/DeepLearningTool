#include "model/ModelTaskController.h"

#include "common/Utils.h"
#include "data/DataManager.h"
#include "data/DatasetExportSource.h"
#include "data/DataOperationWorkflow.h"
#include "model/ExternalModelTaskRunner.h"
#include "model/IModel.h"
#include "model/IModelConfig.h"
#include "model/IParams.h"
#include "model/ModelManager.h"
#include "model/ModelTaskPreparationService.h"
#include "model/TaskCommunication.h"
#include "model/TaskManager.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <memory>
#include <utility>

namespace dltool::model {
using common::setError;

struct ModelTaskController::TaskContext
{
    int                 task_id{-1};
    QString             model_uuid;
    QString             model_name;
    ModelTaskType       task_type{ModelTaskType::Unknown};
    IModel             *model{nullptr};
    FrameworkDefinition framework;
};

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
    connect(external_task_runner_.get(), &ExternalModelTaskRunner::taskStartFailed, this,
            &ModelTaskController::handleExternalTaskStartFailed);
    if (task_manager_ != nullptr)
    {
        connect(task_manager_, &TaskManager::taskStopRequested, this, &ModelTaskController::handleTaskStopRequested);
        connect(task_manager_, &TaskManager::taskMessageReceived, this, &ModelTaskController::handleTaskMessage);
    }
}

ModelTaskController::~ModelTaskController() = default;

int ModelTaskController::addModelTask(const QString &model_uuid, ModelTaskType task_type)
{
    spdlog::info("添加模型任务请求, uuid: {}, 类型: {}", model_uuid.toUtf8().constData(),
                 modelTaskKey(task_type).toUtf8().constData());
    TaskContext context;
    QString     err_msg;
    if (!buildContext(model_uuid, task_type, -1, context, &err_msg))
    {
        spdlog::error("添加模型任务失败: {}", err_msg.toUtf8().constData());
        return -1;
    }
    const int task_id = ensureTaskRecord(context);
    if (task_id >= 0)
        spdlog::info("模型任务添加成功, task_id: {}, 模型: {}, 类型: {}", task_id,
                     context.model_name.toUtf8().constData(), modelTaskKey(task_type).toUtf8().constData());
    return task_id;
}

int ModelTaskController::startModelTask(const QString &model_uuid, ModelTaskType task_type)
{
    spdlog::info("启动模型任务请求, uuid: {}, 类型: {}", model_uuid.toUtf8().constData(),
                 modelTaskKey(task_type).toUtf8().constData());
    TaskContext context;
    QString     err_msg;
    if (!buildContext(model_uuid, task_type, -1, context, &err_msg))
    {
        spdlog::error("启动模型任务失败: {}", err_msg.toUtf8().constData());
        return -1;
    }

    const int task_id = ensureTaskRecord(context);
    if (task_id < 0)
        return -1;

    if (!startTask(task_id))
    {
        spdlog::error("启动模型任务失败, task_id: {}, 模型: {}, 类型: {}", task_id,
                      context.model_name.toUtf8().constData(), modelTaskKey(task_type).toUtf8().constData());
        return -1;
    }
    spdlog::info("模型任务启动请求已提交, task_id: {}, 模型: {}, 类型: {}", task_id,
                 context.model_name.toUtf8().constData(),
                 modelTaskKey(task_type).toUtf8().constData());
    return task_id;
}

bool ModelTaskController::stopModelTask(const QString &model_uuid, ModelTaskType task_type)
{
    spdlog::info("停止模型任务请求, uuid: {}, 类型: {}", model_uuid.toUtf8().constData(),
                 modelTaskKey(task_type).toUtf8().constData());
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr)
        return false;

    const int task_id = task_manager_->tasks()->findModelTask(model_uuid.trimmed(), task_type, false);
    if (task_id < 0)
        return false;
    const bool stopped = stopTask(task_id);
    spdlog::info("停止模型任务{}, task_id: {}", stopped ? "成功" : "失败", task_id);
    return stopped;
}

bool ModelTaskController::deleteModelTask(const QString &model_uuid, ModelTaskType task_type)
{
    spdlog::info("删除模型任务请求, uuid: {}, 类型: {}", model_uuid.toUtf8().constData(),
                 modelTaskKey(task_type).toUtf8().constData());
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr)
        return false;

    const int task_id = task_manager_->tasks()->findModelTask(model_uuid.trimmed(), task_type, false);
    if (task_id < 0)
        return false;
    const bool deleted = deleteTask(task_id);
    spdlog::info("删除模型任务{}, task_id: {}", deleted ? "成功" : "失败", task_id);
    return deleted;
}

bool ModelTaskController::buildContext(const QString &model_uuid, ModelTaskType task_type, int task_id,
                                       TaskContext &context, QString *err_msg) const
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
        return setError(err_msg, QString("框架未注册: %1").arg(model->frameworkName()));
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

int ModelTaskController::ensureTaskRecord(const TaskContext &context)
{
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr)
        return -1;

    const int existing_task_id = task_manager_->tasks()->findModelTask(context.model_uuid, context.task_type, false);
    if (existing_task_id >= 0)
    {
        owned_task_ids_.insert(existing_task_id);
        return existing_task_id;
    }

    const bool supports_pause = !context.framework.supportsExternalTask(context.task_type);
    const int  task_id
        = task_manager_->addTask(context.model_uuid, context.model_name, context.task_type, supports_pause);
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

    TaskContext context;
    QString     err_msg;
    if (!buildContext(task.model_uuid, task.task_type, task_id, context, &err_msg))
    {
        failTask(task_id, err_msg);
        return false;
    }

    if (!context.framework.supportsExternalTask(context.task_type))
    {
        if (!task_manager_->startTask(task_id))
            return false;

        touchTaskModelModifiedTime(task_id);
        return true;
    }

    QString server_err;
    if (!task_manager_->ensureTaskServer(&server_err))
    {
        failTask(task_id, QString("任务通信服务启动失败: %1").arg(server_err));
        return false;
    }

    ModelTaskPreparationRequest request;
    if (!buildPreparationRequest(context, request, &err_msg))
    {
        failTask(task_id, err_msg);
        return false;
    }

    if (!task_manager_->startTask(task_id))
        return false;

    touchTaskModelModifiedTime(task_id);

    const auto process_spec = std::make_shared<ExternalProcessSpec>();

    dltool::data::DataOperationWorkflow::Options options;
    options.title            = QStringLiteral("准备模型任务");
    options.start_message    = QString("准备模型任务: %1").arg(modelTaskDisplayName(context.task_type));
    options.initial_progress = 5;

    const int     method      = method_;
    const QString project_dir = project_dir_;
    const auto prepare_task = [method, project_dir, request, process_spec](
                                  const dltool::data::DatasetExportSource *dataset_source,
                                  dltool::data::DataOperationWorkflow::Result &result)
        {
            ModelTaskPreparationService preparation(method, project_dir);
            QString                     preparation_error;
            if (!preparation.prepare(request, dataset_source, *process_spec, &preparation_error))
            {
                result.error = preparation_error;
                return;
            }
            result.success = true;
        };
    const auto completion = [this, task_id, process_spec](const dltool::data::DataOperationWorkflow::Result &result)
    { handlePreparedTask(task_id, process_spec, result.success, result.error); };

    if (describeModelTask(context.task_type).requires_dataset_export)
    {
        if (data_manager_ == nullptr)
        {
            const QString message = QStringLiteral("数据管理器为空");
            touchTaskModelModifiedTime(task_id);
            failTask(task_id, message);
            return false;
        }

        dltool::data::DatasetExportRequest export_request;
        export_request.dataset_ids = selectedDatasetIds(request.selections);
        data_manager_->runDatasetExportAsync(
            this, std::move(export_request), std::move(options),
            [prepare_task](const dltool::data::DatasetExportSource &dataset_source,
                           dltool::data::DataOperationWorkflow::Result &result)
            { prepare_task(&dataset_source, result); },
            completion);
    }
    else
    {
        dltool::data::DataOperationWorkflow::start(
            this, std::move(options),
            [prepare_task](dltool::data::DataOperationWorkflow::Result &result)
            { prepare_task(nullptr, result); },
            completion);
    }

    spdlog::info("模型任务已进入后台准备, task_id: {}, 模型: {}, 类型: {}", task_id,
                 context.model_name.toUtf8().constData(), modelTaskKey(context.task_type).toUtf8().constData());
    return true;
}

bool ModelTaskController::buildPreparationRequest(const TaskContext &context,
                                                  ModelTaskPreparationRequest &request, QString *err_msg) const
{
    request = {};
    if (context.model == nullptr)
        return setError(err_msg, QString("模型实例为空"));
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr)
        return setError(err_msg, QString("任务管理器为空"));

    request.task_id          = context.task_id;
    request.task_type        = context.task_type;
    request.framework        = context.framework;
    request.task_server_host = task_manager_->taskServerHost();
    request.task_server_port = task_manager_->taskServerPort();
    request.selections       = modelDatasetSelectionsSnapshot(context.model);
    request.model_config.model_uuid        = context.model_uuid;
    request.model_config.model_name        = context.model_name;
    request.model_config.framework_name    = context.model->frameworkName();
    request.model_config.model_architecture = context.model->modelArchitecture();

    if (const IModelConfig *config = context.model->config(); config != nullptr)
    {
        if (const ITrainParams *params = config->trainParams(); params != nullptr)
            request.model_config.train_params = params->valuesMap();
        if (const ITestParams *params = config->testParams(); params != nullptr)
            request.model_config.test_params = params->valuesMap();
    }

    return true;
}

void ModelTaskController::handlePreparedTask(int task_id, const std::shared_ptr<ExternalProcessSpec> &process_spec,
                                             const bool success, const QString &error)
{
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr)
        return;

    const TaskTableModel::TaskSnapshot task = task_manager_->tasks()->taskSnapshotForId(task_id);
    if (!task.isValid() || task.status != TaskTableModel::Running)
        return;

    if (!success)
    {
        const QString message = error.isEmpty() ? QStringLiteral("准备模型任务失败") : error;
        touchTaskModelModifiedTime(task_id);
        failTask(task_id, message);
        return;
    }

    QString start_error;
    if (process_spec == nullptr || !startExternalTask(*process_spec, &start_error))
    {
        const QString message = start_error.isEmpty() ? QStringLiteral("启动外部模型任务失败") : start_error;
        touchTaskModelModifiedTime(task_id);
        failTask(task_id, message);
        return;
    }

    spdlog::info("模型任务进程启动请求已发送, task_id: {}", task_id);
}

bool ModelTaskController::stopTask(int task_id)
{
    if (task_manager_ == nullptr)
        return false;

    const bool had_external_process
        = external_task_runner_ != nullptr && external_task_runner_->hasRunningTask(task_id);
    const bool stop_requested = task_manager_->stopTask(task_id);
    if (stop_requested && !had_external_process)
    {
        task_manager_->markTaskStopped(task_id);
        touchTaskModelModifiedTime(task_id);
    }
    if (stop_requested)
        spdlog::info("模型任务停止信号已发送, task_id: {}", task_id);
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
        spdlog::info("模型任务删除{}, task_id: {}", deleted_from_runner && deleted_from_table ? "成功" : "失败",
                     task_id);
        return deleted_from_runner && deleted_from_table;
    }
    if (deleted_from_table)
        owned_task_ids_.erase(task_id);
    spdlog::info("模型任务删除{}, task_id: {}", deleted_from_table ? "成功" : "失败", task_id);
    return deleted_from_table;
}

void ModelTaskController::handleTaskMessage(const TaskMessage &message)
{
    if (model_manager_ == nullptr || task_manager_ == nullptr || task_manager_->tasks() == nullptr
        || message.task_id < 0)
        return;
    if (message.type == TaskMessageType::Log || message.type == TaskMessageType::Command)
        return;

    const TaskTableModel::TaskSnapshot task = task_manager_->tasks()->taskSnapshotForId(message.task_id);
    if (!task.isValid() || (!isTrainModelTask(task.task_type) && !isTestModelTask(task.task_type)))
        return;

    const ModelManager::ModelRecordView model_record = model_manager_->modelRecordViewForUuid(task.model_uuid);
    const FrameworkDefinition          framework     = registeredFramework(method_, model_record.framework_name);
    if (!framework.name.isEmpty() && !framework.write_to_database)
        return;

    QString phase = message.payload.value(QStringLiteral("phase")).toString().trimmed().toLower();
    if (phase != QStringLiteral("train") && phase != QStringLiteral("test"))
        phase = isTrainModelTask(task.task_type) ? QStringLiteral("train") : QStringLiteral("test");

    QVariantMap updates;
    if (message.status == TaskProtocolStatus::Running || message.payload.contains(QStringLiteral("started")))
        updates.insert(QStringLiteral("started"),
                       message.payload.value(QStringLiteral("started"), true).toBool());
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

    const bool terminal = message.status == TaskProtocolStatus::Stopped
                       || message.status == TaskProtocolStatus::Finished
                       || message.status == TaskProtocolStatus::Failed
                       || message.status == TaskProtocolStatus::Error;
    if (terminal && !framework.supportsExternalTask(task.task_type))
        touchTaskModelModifiedTime(message.task_id);

    if (updates.isEmpty())
        return;

    const QVariantMap current_model = model_manager_->modelRecordForUuid(task.model_uuid);
    const QVariantMap extra_data    = current_model.value(QStringLiteral("extra_data")).toMap();
    QVariantMap       section       = extra_data.value(phase).toMap();
    for (auto it = updates.cbegin(); it != updates.cend(); ++it)
        section.insert(it.key(), it.value());

    QVariantMap state_update;
    state_update.insert(phase, section);
    QString err_msg;
    if (!model_manager_->updateModelExtraData(task.model_uuid, state_update, &err_msg))
    {
        spdlog::error("保存模型任务状态失败, task_id: {}, uuid: {}, 错误: {}", message.task_id,
                      task.model_uuid.toUtf8().constData(), err_msg.toUtf8().constData());
    }
}

bool ModelTaskController::startExternalTask(const ExternalProcessSpec &process_spec, QString *err_msg)
{
    if (task_manager_ == nullptr || external_task_runner_ == nullptr)
        return setError(err_msg, QString("模型任务控制器未初始化"));

    return external_task_runner_->start(process_spec, err_msg);
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
    {
        spdlog::error("模型任务 {} 失败: {}", task_id, message.toUtf8().constData());
        ui::SignalHelper::notifyError(QString("模型任务 %1 失败").arg(task_id), message);
    }
    if (task_manager_ != nullptr)
        task_manager_->failTask(task_id);
}

void ModelTaskController::touchTaskModelModifiedTime(const int task_id) const
{
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr || model_manager_ == nullptr)
        return;

    const TaskTableModel::TaskSnapshot task = task_manager_->tasks()->taskSnapshotForId(task_id);
    if (!task.isValid())
        return;

    QString err_msg;
    if (!model_manager_->touchModelModifiedTime(task.model_uuid, &err_msg))
    {
        spdlog::error("更新任务对应模型修改时间失败, task_id: {}, uuid: {}, 错误: {}", task_id,
                      task.model_uuid.toUtf8().constData(), err_msg.toUtf8().constData());
    }
}

void ModelTaskController::handleTaskStopRequested(int task_id)
{
    if (external_task_runner_ == nullptr || !taskBelongsToCurrentModelManager(task_id))
        return;
    external_task_runner_->stop(task_id);
}

void ModelTaskController::handleExternalTaskStartFailed(const int task_id, const QString &error)
{
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr || !taskBelongsToCurrentModelManager(task_id))
        return;

    const TaskTableModel::TaskSnapshot task = task_manager_->tasks()->taskSnapshotForId(task_id);
    if (!task.isValid() || task.status != TaskTableModel::Running)
        return;

    const QString message = error.isEmpty() ? QStringLiteral("外部模型任务进程启动失败") : error;
    touchTaskModelModifiedTime(task_id);
    failTask(task_id, message);
}

void ModelTaskController::handleExternalTaskFinished(int task_id, int exit_code, bool normal_exit, bool stop_requested)
{
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr || !taskBelongsToCurrentModelManager(task_id))
        return;

    touchTaskModelModifiedTime(task_id);

    if (stop_requested || (normal_exit && exit_code == 2))
        task_manager_->markTaskStopped(task_id);
    else if (normal_exit && exit_code == 0)
        task_manager_->finishTask(task_id);
    else
    {
        const TaskTableModel::TaskSnapshot task = task_manager_->tasks()->taskSnapshotForId(task_id);
        // A task process may report an error through the task server before it exits. In that
        // case the router has already reported the failure, so do not display the same alert twice.
        if (task.status != TaskTableModel::Failed)
        {
            const QString task_name = task.isValid() ? modelTaskDisplayName(task.task_type) : QString("模型任务");
            const QString message   = normal_exit
                                        ? QString("%1失败（退出码 %2），请查看模型日志。").arg(task_name).arg(exit_code)
                                        : QString("%1异常退出，请查看模型日志。").arg(task_name);
            failTask(task_id, message);
        }
    }
}

} // namespace dltool::model
