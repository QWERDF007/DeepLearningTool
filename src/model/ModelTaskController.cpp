#include "model/ModelTaskController.h"

#include "common/Utils.h"
#include "common/YamlUtils.h"
#include "core/CoreDef.h"
#include "data/DataManager.h"
#include "data/DataOperationWorkflow.h"
#include "data/DatasetExportSource.h"
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

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThreadPool>
#include <algorithm>
#include <exception>
#include <memory>
#include <utility>

namespace dltool::model {
using common::cleanPath;
using common::setError;

namespace {

/**
 * @brief extra_data 写库节流间隔。
 *
 * Python 按 iter 高频上报时，GUI 线程逐条写 SQLite 会成为磁盘 IO 瓶颈；
 * 指标字段先合并进内存缓冲，最多每 1 秒落库一次。
 */
constexpr int kExtraFlushIntervalMs = 1000;

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
    test_task_repository_.setProjectDatabasePath(model_manager_ != nullptr ? model_manager_->projectDatabasePath()
                                                                           : QString());
    connect(external_task_runner_.get(), &ExternalModelTaskRunner::taskStarted, this,
            &ModelTaskController::handleExternalTaskStarted);
    connect(external_task_runner_.get(), &ExternalModelTaskRunner::taskFinished, this,
            &ModelTaskController::handleExternalTaskFinished);
    connect(external_task_runner_.get(), &ExternalModelTaskRunner::taskStartFailed, this,
            &ModelTaskController::handleExternalTaskStartFailed);
    if (task_manager_ != nullptr)
    {
        connect(task_manager_, &TaskManager::taskStartRequested, this, &ModelTaskController::handleTaskStartRequested);
        connect(task_manager_, &TaskManager::taskStopRequested, this, &ModelTaskController::handleTaskStopRequested);
        connect(task_manager_, &TaskManager::taskMessageReceived, this, &ModelTaskController::handleTaskMessage);
    }
    // extra_data 写库节流定时器：高频进度事件先合并进内存缓冲，由该定时器
    // 统一落库，避免每条 Python 消息都触发一次 SQLite 写入。
    extra_flush_timer_ = new QTimer(this);
    extra_flush_timer_->setInterval(kExtraFlushIntervalMs);
    connect(extra_flush_timer_, &QTimer::timeout, this, &ModelTaskController::flushPendingExtraUpdates);
}

ModelTaskController::~ModelTaskController()
{
    shutdown();
}

void ModelTaskController::shutdown()
{
    // 项目关闭前冲刷全部待写入的状态更新，避免高频事件缓冲丢失。
    flushPendingExtraUpdates();
    if (task_manager_ != nullptr)
    {
        const int count = task_manager_->rowCount();
        for (int row = 0; row < count; ++row)
        {
            const QModelIndex        index   = task_manager_->index(row, 0);
            const int                task_id = task_manager_->data(index, TaskManager::TaskIdRole).toInt();
            const TaskManager::Task *task    = task_manager_->findTask(task_id);
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
                    const int task_id
                        = task_manager_->data(task_manager_->index(row, 0), TaskManager::TaskIdRole).toInt();
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
    QString   error;
    const int task_id = ensureTaskRecord(model_uuid, task_type, {}, {}, &error);
    if (task_id < 0)
        spdlog::error("添加模型任务失败: {}", error.toUtf8().constData());
    return task_id;
}

int ModelTaskController::startModelTask(const QString &model_uuid, const ModelTaskType task_type)
{
    QString   error;
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
    QString   error;
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
    const int task_id
        = task_manager_->findModelTask(model_uuid.trimmed(), ModelTaskType::Test, test_task_uuid.trimmed(), false);
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

    QString resolved_scope      = scope_uuid.trimmed();
    QString resolved_scope_name = scope_name.trimmed();
    if (isTrainModelTask(task_type))
        resolved_scope = QStringLiteral("train");
    // 普通测试任务必须有 UUID 测试任务记录；小样本框架（few_shot 能力位）
    // 的测试任务没有 UUID，直接进入无作用域流程。
    if (isTestModelTask(task_type) && resolved_scope.isEmpty() && !framework.isFewShot())
    {
        setError(err_msg, QString("普通测试任务必须绑定测试任务 UUID"));
        return -1;
    }
    ModelTestTaskDefinition resolved_definition;
    bool                    has_resolved_definition = false;
    if (isTestModelTask(task_type) && !resolved_scope.isEmpty())
    {
        if (!test_task_repository_.loadTask(record.name, resolved_scope, resolved_definition, err_msg))
            return -1;
        resolved_scope_name     = resolved_definition.name;
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
    QString                   config_path;
    QString                   log_path;
    if (isTrainModelTask(task_type))
    {
        config_path = storage.modelDatabasePath(record.name);
        log_path    = storage.trainLogPath(record.name);
    }
    else if (has_resolved_definition)
    {
        config_path = storage.testTaskDatabasePath(record.name, resolved_definition.directory_name);
        log_path    = storage.testTaskLogPath(record.name, resolved_definition.uuid);
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

    const ModelManager::ModelRecordView record    = model_manager_->modelRecordViewForUuid(task->model_uuid);
    const FrameworkDefinition           framework = registeredFramework(method_, record.framework_name);
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
    const auto request_ptr  = std::make_shared<ModelTaskRequest>(std::move(request));

    dltool::data::DataOperationWorkflow::Options options;
    options.title            = QString("准备模型任务");
    options.start_message    = QString("准备模型任务: %1").arg(modelTaskDisplayName(request_ptr->task_type));
    options.initial_progress = 5;

    const int     method      = method_;
    const QString project_dir = project_dir_;
    const auto    prepare
        = [method, project_dir, request_ptr, process_spec](const dltool::data::DatasetExportSource     *dataset_source,
                                                           dltool::data::DataOperationWorkflow::Result &result)
    {
        ModelTaskRequest &request = *request_ptr;
        QString           error;
        if (!prepareModelTask(method, project_dir, request, dataset_source, *process_spec, &error))
        {
            result.error = error;
            return;
        }
        result.success = true;
    };
    const auto completion = [this, task_id, process_spec](const dltool::data::DataOperationWorkflow::Result &result)
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
            [prepare](const dltool::data::DatasetExportSource     &source,
                      dltool::data::DataOperationWorkflow::Result &result) { prepare(&source, result); },
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

    request.task_id                         = task->id;
    request.task_type                       = task->type;
    request.scope_uuid                      = task->scope_uuid;
    request.scope_name                      = task->scope_name;
    request.evaluation_method               = evaluation::fromProjectMethod(method_);
    request.framework                       = framework;
    request.task_server_host                = task_manager_->taskServerHost();
    request.task_server_port                = task_manager_->taskServerPort();
    request.project_database_path           = model_manager_->projectDatabasePath();
    request.selections                      = modelDatasetSelections(model);
    request.model_config.model_uuid         = record.uuid;
    request.model_config.model_name         = record.name;
    request.model_config.framework_name     = record.framework_name;
    request.model_config.method             = evaluation::methodKey(request.evaluation_method);
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
        QString                 error;
        if (!test_task_repository_.loadTask(record.name, task->scope_uuid, definition, &error))
            return setError(err_msg, error);
        request.selections                          = {};
        request.selections.test                     = definition.dataset_selection;
        request.model_config.test_params            = definition.test_params;
        request.scope_name                          = definition.name;
        request.model_config.scope_name             = definition.name;
        request.model_config.task_directory         = definition.directory_name;
        request.model_config.test_dataset_selection = definition.dataset_selection;
        request.model_config.created_at             = definition.created_at;
        request.model_config.modified_at            = definition.modified_at;
    }
    return true;
}

void ModelTaskController::handlePreparedTask(const int                                   task_id,
                                             const std::shared_ptr<ExternalProcessSpec> &process_spec,
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

void ModelTaskController::failTask(const int task_id, const QString &message)
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

void ModelTaskController::syncTaskModelState(const int task_id)
{
    // extra_data 只是 TaskManager 状态的持久化投影：这里只做状态投影与
    // 待写入字段的合并落库，不维护独立进度值（见 flushModelState）。
    flushModelState(task_id);
}

void ModelTaskController::applyTaskStateToSection(const TaskManager::Task &task, const bool terminal,
                                                  const bool completed, QVariantMap &section)
{
    // 进度以 TaskManager 为唯一权威：Finished 归一化为 100，其余状态（含
    // Stopped/Failed）直接投影任务记录值，避免模型页与任务中心表格出现
    // 不一致的进度（例如训练后停留在 90% 而任务中心为 100%）。
    section.insert(QStringLiteral("progress"), completed ? 100 : task.progress);

    if (terminal)
    {
        section.insert(QStringLiteral("started"), false);
        section.insert(QStringLiteral("status"), taskManagerStatusName(task.status));
    }
    else if (task.status == TaskManager::Running || task.status == TaskManager::Paused
             || task.status == TaskManager::Stopping)
    {
        section.insert(QStringLiteral("started"), true);
        section.insert(QStringLiteral("status"), taskManagerStatusName(task.status));
    }
}

void ModelTaskController::flushModelState(const int task_id)
{
    if (task_manager_ == nullptr || model_manager_ == nullptr)
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr || (!isTrainModelTask(task->type) && !isTestModelTask(task->type)))
    {
        pending_extra_updates_.remove(task_id);
        return;
    }

    // 取出本次待合并的指标字段（epoch/iter/lr/loss/elapsed/eta/metrics/
    // message/status）。progress 与 started 不在此缓冲，由状态投影写入。
    const QVariantMap updates = pending_extra_updates_.take(task_id);

    // The top-level model data is keyed by the software task type.  A train
    // runner may report validation/evaluation as phase "test", but that is
    // still part of the training task and must not update the separate Test
    // page's state.
    const bool train_scope = isTrainModelTask(task->type);
    // 小样本测试任务（框架能力位 few_shot）没有 UUID 测试任务记录，其状态
    // 投影到 extra_data.test 顶层 section；能力来源为框架注册表。
    const bool legacy_few_shot_test
        = isTestModelTask(task->type) && task->scope_uuid.trimmed().isEmpty()
       && registeredFramework(method_, model_manager_->modelRecordViewForUuid(task->model_uuid).framework_name)
              .isFewShot();
    const QString phase = train_scope ? QStringLiteral("train") : QStringLiteral("test_tasks");

    const QVariantMap current_model = model_manager_->modelRecordForUuid(task->model_uuid);
    const QVariantMap extra_data    = current_model.value(QStringLiteral("extra_data")).toMap();
    QVariantMap       test_tasks    = extra_data.value(QStringLiteral("test_tasks")).toMap();
    QVariantMap       section = train_scope ? extra_data.value(phase).toMap()
                                            : (legacy_few_shot_test ? extra_data.value(QStringLiteral("test")).toMap()
                                                                    : test_tasks.value(task->scope_uuid).toMap());
    for (auto it = updates.cbegin(); it != updates.cend(); ++it) section.insert(it.key(), it.value());

    // 状态投影：进度/开始标记以任务中心为准，与 TaskManager 表格逐帧一致。
    applyTaskStateToSection(*task, TaskManager::isTerminal(task->status), task->status == TaskManager::Finished,
                            section);

    QString     error;
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
        spdlog::error("保存模型任务状态失败, task_id: {}, uuid: {}, 错误: {}", task_id,
                      task->model_uuid.toUtf8().constData(), error.toUtf8().constData());
    }
}

void ModelTaskController::flushPendingExtraUpdates()
{
    // 定时器触发或 shutdown 时冲刷全部脏任务；每个任务最多一次写库。
    const QList<int> task_ids = pending_extra_updates_.keys();
    for (const int task_id : task_ids) flushModelState(task_id);
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

    // 高频进度消息只合并进内存缓冲，由节流定时器统一落库（≤1 次/秒/任务）。
    // TaskManager 表格仍由 TaskManager 每事件即时更新（纯内存，无磁盘 IO）。
    // 消息只携带指标字段；progress/started 一律由任务状态投影写入（见
    // applyTaskStateToSection），不在此处维护独立进度值。
    QVariantMap &pending = pending_extra_updates_[message.task_id];
    for (const QString &key : {QStringLiteral("epoch"), QStringLiteral("iter"), QStringLiteral("lr"),
                               QStringLiteral("loss"), QStringLiteral("elapsed"), QStringLiteral("eta")})
    {
        if (message.payload.contains(key))
            pending.insert(key, message.payload.value(key).toString());
    }
    if (message.payload.contains(QStringLiteral("metrics")))
        pending.insert(QStringLiteral("metrics"), message.payload.value(QStringLiteral("metrics")).toString());
    if (!message.message.isEmpty())
        pending.insert(QStringLiteral("message"), message.message);

    const QString status = taskProtocolStatusName(message.status);
    if (!status.isEmpty())
        pending.insert(QStringLiteral("status"), status);

    // 终态事件必须立即冲刷：确保最后一条状态（started=false、Finished 的
    // 100%）不因节流窗口被丢弃，也避免迟到事件覆盖终态。
    const bool terminal = message.status == TaskProtocolStatus::Stopped
                       || message.status == TaskProtocolStatus::Finished || message.status == TaskProtocolStatus::Failed
                       || message.status == TaskProtocolStatus::Error;
    if (terminal)
    {
        touchTaskModelModifiedTime(message.task_id);
        flushModelState(message.task_id);
        return;
    }

    if (!extra_flush_timer_->isActive())
        extra_flush_timer_->start();
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
