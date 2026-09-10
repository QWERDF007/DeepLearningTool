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
#include "database/ModelTaskDataBase.h"
#include "model/IParams.h"
#include "model/ModelDatasetSelection.h"
#include "model/ModelManager.h"
#include "model/ModelRegistry.h"
#include "model/ModelStorageService.h"
#include "model/TaskManager.h"
#include "ui/SignalHelper.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <spdlog/spdlog.h>

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

TaskManager::TaskStatus taskManagerStatusFromName(const QString &name)
{
    const QString lower = name.trimmed().toLower();
    if (lower == QStringLiteral("finished"))
        return TaskManager::Finished;
    if (lower == QStringLiteral("stopped"))
        return TaskManager::Stopped;
    if (lower == QStringLiteral("failed") || lower == QStringLiteral("error"))
        return TaskManager::Failed;
    if (lower == QStringLiteral("running"))
        return TaskManager::Running;
    if (lower == QStringLiteral("stopping"))
        return TaskManager::Stopping;
    if (lower == QStringLiteral("preparing"))
        return TaskManager::Preparing;
    return TaskManager::Pending;
}

qint64 parseDurationText(const QString &text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
    if (parts.size() == 3)
    {
        bool ok_h = false, ok_m = false, ok_s = false;
        const qint64 h = parts[0].toLongLong(&ok_h);
        const qint64 m = parts[1].toLongLong(&ok_m);
        const qint64 s = parts[2].toLongLong(&ok_s);
        if (ok_h && ok_m && ok_s)
            return std::max<qint64>(0, h * 3600 + m * 60 + s);
    }
    return 0;
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
        connect(task_manager_, &TaskManager::taskRunningTimeChanged, this,
                &ModelTaskController::handleTaskRunningTimeChanged);
        connect(task_manager_, &TaskManager::taskMessageReceived, this, &ModelTaskController::handleTaskMessage);
    }
    // extra_data 写库节流定时器：高频进度事件先合并进内存缓冲，由该定时器
    // 统一落库，避免每条 Python 消息都触发一次 SQLite 写入。
    extra_flush_timer_ = new QTimer(this);
    extra_flush_timer_->setInterval(kExtraFlushIntervalMs);
    connect(extra_flush_timer_, &QTimer::timeout, this, &ModelTaskController::flushPendingExtraUpdates);

    restoreModelTasks();
}

ModelTaskController::~ModelTaskController()
{
    shutdown();
}

void ModelTaskController::shutdown()
{
    if (shutting_down_)
        return;
    shutting_down_ = true;

    if (extra_flush_timer_ != nullptr)
        extra_flush_timer_->stop();

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
            if (task == nullptr || TaskManager::isTerminal(task->status))
                continue;

            task_manager_->stopTask(task_id);
        }
    }

    for (const auto &operation : preparation_operations_)
    {
        if (operation != nullptr)
            operation->requestCancel();
    }

    if (external_task_runner_ != nullptr)
        external_task_runner_->shutdown();

    QList<dltool::data::DataOperationWorkflow::HandlePtr> preparation_operations;
    preparation_operations.reserve(preparation_operations_.size());
    for (const auto &operation : preparation_operations_)
        preparation_operations.push_back(operation);
    dltool::data::DataOperationWorkflow::waitForCompletions(preparation_operations);

    // External process signals may have been delivered while waiting. Any
    // remaining active record is now stopped explicitly before its project
    // owner is released.
    if (task_manager_ != nullptr)
    {
        const int count = task_manager_->rowCount();
        for (int row = 0; row < count; ++row)
        {
            const QModelIndex        index   = task_manager_->index(row, 0);
            const int                task_id = task_manager_->data(index, TaskManager::TaskIdRole).toInt();
            const TaskManager::Task *task    = task_manager_->findTask(task_id);
            if (task == nullptr || TaskManager::isTerminal(task->status))
                continue;

            task_manager_->markTaskStopped(task_id);
            syncTaskModelState(task_id);
        }
    }
    preparation_operations_.clear();
}

void ModelTaskController::beginShutdown()
{
    shutdown_requested_ = true;
    if (external_task_runner_ != nullptr)
        external_task_runner_->shutdown();
    for (const auto &operation : preparation_operations_)
        if (operation != nullptr)
            operation->requestCancel();
    if (task_manager_ != nullptr)
    {
        const int count = task_manager_->rowCount();
        for (int row = 0; row < count; ++row)
        {
            const int task_id = task_manager_->data(task_manager_->index(row, 0), TaskManager::TaskIdRole).toInt();
            if (const auto *task = task_manager_->findTask(task_id); task != nullptr
                && !TaskManager::isTerminal(task->status))
                task_manager_->stopTask(task_id);
        }
    }
}

int ModelTaskController::addModelTask(const QString &model_uuid, const ModelTaskType task_type)
{
    if (shutting_down_)
        return -1;

    QString   error;
    const int task_id = ensureTaskRecord(model_uuid, task_type, {}, {}, &error);
    if (task_id < 0)
        spdlog::error("添加模型任务失败: {}", error.toUtf8().constData());
    return task_id;
}

int ModelTaskController::startModelTask(const QString &model_uuid, const ModelTaskType task_type)
{
    if (shutting_down_)
        return -1;

    QString   error;
    const int task_id = ensureTaskRecord(model_uuid, task_type, {}, {}, &error);
    if (task_id < 0)
    {
        spdlog::error("启动模型任务失败: {}", error.toUtf8().constData());
        return -1;
    }
    // 训练/内部子任务启动时立即清空上一次的状态（epoch/iter/lr/loss/
    // elapsed/eta/metrics 等），进度重置为 0，避免界面保留旧值直到新上报到达。
    const bool train_task = isTrainModelTask(task_type);
    const bool b2m_task   = (task_type == ModelTaskType::BoxToMask);
    if ((train_task || b2m_task) && model_manager_ != nullptr)
    {
        QString reset_error;
        model_manager_->resetModelTaskState(
            model_uuid, b2m_task ? QStringLiteral("box_to_mask") : QStringLiteral("train"),
            {
                QStringLiteral("epoch"), QStringLiteral("iter"), QStringLiteral("lr"), QStringLiteral("loss"),
                QStringLiteral("elapsed"), QStringLiteral("elapsed_seconds"), QStringLiteral("eta"),
                QStringLiteral("metrics"), QStringLiteral("message"),
                QStringLiteral("status"), QStringLiteral("started"), QStringLiteral("phase"),
                QStringLiteral("phase_progress"), QStringLiteral("run_id")
            },
            {{QStringLiteral("progress"), 0}}, &reset_error);
    }
    return task_manager_->startTask(task_id) ? task_id : -1;
}

int ModelTaskController::startModelTestTask(const QString &model_uuid, const QString &test_task_uuid)
{
    if (shutting_down_)
        return -1;

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
    if (shutting_down_ || shutdown_requested_ || task_manager_ == nullptr)
        return false;

    const int task_id = task_manager_->findModelTask(model_uuid.trimmed(), task_type, false);
    return task_id >= 0 && stopTask(task_id);
}

bool ModelTaskController::stopModelTestTask(const QString &model_uuid, const QString &test_task_uuid)
{
    if (shutting_down_ || task_manager_ == nullptr)
        return false;
    const int task_id
        = task_manager_->findModelTask(model_uuid.trimmed(), ModelTaskType::Test, test_task_uuid.trimmed(), false);
    return task_id >= 0 && stopTask(task_id);
}

bool ModelTaskController::deleteModelTask(const QString &model_uuid, const ModelTaskType task_type)
{
    if (shutting_down_ || task_manager_ == nullptr)
        return false;

    const int task_id = task_manager_->findModelTask(model_uuid.trimmed(), task_type, true);
    return task_id >= 0 && deleteTask(task_id);
}

void ModelTaskController::restoreModelTasks()
{
    if (model_manager_ == nullptr || task_manager_ == nullptr)
        return;

    const int count = model_manager_->rowCount();
    const ModelStorageService storage(project_dir_);

    for (int row = 0; row < count; ++row)
    {
        const QVariantMap model_map = model_manager_->modelAt(row);
        const QString uuid = model_map.value(QStringLiteral("uuid")).toString().trimmed();
        const QString name = model_map.value(QStringLiteral("name")).toString().trimmed();
        const QString framework_name = model_map.value(QStringLiteral("framework_name")).toString().trimmed();
        const QVariantMap extra_data = model_map.value(QStringLiteral("extra_data")).toMap();

        if (uuid.isEmpty() || name.isEmpty())
            continue;

        const FrameworkDefinition framework = registeredFramework(method_, framework_name);

        // 1. 恢复训练任务
        if (extra_data.contains(QStringLiteral("train")))
        {
            const QVariantMap train_section = extra_data.value(QStringLiteral("train")).toMap();
            const QString status_str = train_section.value(QStringLiteral("status")).toString().trimmed();
            if (!status_str.isEmpty())
            {
                TaskManager::TaskStatus status = taskManagerStatusFromName(status_str);
                if (status == TaskManager::Running || status == TaskManager::Stopping)
                    status = TaskManager::Failed;

                qint64 elapsed_sec = 0;
                if (train_section.contains(QStringLiteral("elapsed_seconds")))
                    elapsed_sec = train_section.value(QStringLiteral("elapsed_seconds")).toLongLong();
                else if (train_section.contains(QStringLiteral("elapsed")))
                    elapsed_sec = parseDurationText(train_section.value(QStringLiteral("elapsed")).toString());

                const int progress = train_section.value(QStringLiteral("progress")).toInt();
                const QString phase = train_section.value(QStringLiteral("phase")).toString();
                const QString run_id = train_section.value(QStringLiteral("run_id")).toString();
                const QString config_path = storage.modelDatabasePath(name);
                const QString log_path = storage.trainLogPath(name);

                task_manager_->restoreTask(uuid, name, ModelTaskType::Train,
                                           QStringLiteral("train"), {}, status, progress,
                                           elapsed_sec, phase, run_id, config_path, log_path);
            }
        }

        // 2. 恢复 BoxToMask 内部子任务
        if (extra_data.contains(QStringLiteral("box_to_mask")))
        {
            const QVariantMap b2m_section = extra_data.value(QStringLiteral("box_to_mask")).toMap();
            const QString status_str = b2m_section.value(QStringLiteral("status")).toString().trimmed();
            if (!status_str.isEmpty())
            {
                TaskManager::TaskStatus status = taskManagerStatusFromName(status_str);
                if (status == TaskManager::Running || status == TaskManager::Stopping)
                    status = TaskManager::Failed;

                qint64 elapsed_sec = 0;
                if (b2m_section.contains(QStringLiteral("elapsed_seconds")))
                    elapsed_sec = b2m_section.value(QStringLiteral("elapsed_seconds")).toLongLong();
                else if (b2m_section.contains(QStringLiteral("elapsed")))
                    elapsed_sec = parseDurationText(b2m_section.value(QStringLiteral("elapsed")).toString());

                const int progress = b2m_section.value(QStringLiteral("progress")).toInt();
                const QString phase = b2m_section.value(QStringLiteral("phase")).toString();
                const QString run_id = b2m_section.value(QStringLiteral("run_id")).toString();
                const QString config_path = storage.modelDatabasePath(name);
                const QString log_path = storage.testTaskLogPath(name, QStringLiteral("fs_sam2_box_to_mask"));

                task_manager_->restoreTask(uuid, name, ModelTaskType::BoxToMask,
                                           QStringLiteral("box_to_mask"), QStringLiteral("BoxToMask"),
                                           status, progress, elapsed_sec, phase, run_id, config_path, log_path);
            }
        }

        // 3. 恢复小样本测试子任务 (few_shot)
        if (framework.isFewShot() && extra_data.contains(QStringLiteral("test")))
        {
            const QVariantMap test_section = extra_data.value(QStringLiteral("test")).toMap();
            const QString status_str = test_section.value(QStringLiteral("status")).toString().trimmed();
            if (!status_str.isEmpty())
            {
                TaskManager::TaskStatus status = taskManagerStatusFromName(status_str);
                if (status == TaskManager::Running || status == TaskManager::Stopping)
                    status = TaskManager::Failed;

                qint64 elapsed_sec = 0;
                if (test_section.contains(QStringLiteral("elapsed_seconds")))
                    elapsed_sec = test_section.value(QStringLiteral("elapsed_seconds")).toLongLong();
                else if (test_section.contains(QStringLiteral("elapsed")))
                    elapsed_sec = parseDurationText(test_section.value(QStringLiteral("elapsed")).toString());

                const int progress = test_section.value(QStringLiteral("progress")).toInt();
                const QString phase = test_section.value(QStringLiteral("phase")).toString();
                const QString run_id = test_section.value(QStringLiteral("run_id")).toString();
                const QString config_path = storage.modelDatabasePath(name);
                const QString log_path = storage.testTaskLogPath(name, QStringLiteral("fs_sam2"));

                task_manager_->restoreTask(uuid, name, ModelTaskType::Test,
                                           {}, {}, status, progress, elapsed_sec, phase, run_id,
                                           config_path, log_path);
            }
        }

        // 4. 恢复普通测试任务 (test_tasks via test_task_repository_ and task.db)
        if (!framework.isFewShot())
        {
            const QList<ModelTestTaskDefinition> test_tasks = test_task_repository_.listTasks(name);
            for (const auto &task_def : test_tasks)
            {
                recoverTestTaskPublish(name, task_def.directory_name);
                const QString task_db_path = storage.testTaskDatabasePath(name, task_def.directory_name);
                database::ModelTaskDataBase task_db(task_db_path);
                QVariantMap execution_state;
                if (!task_db.readExecutionState(execution_state))
                    continue;

                const QString status_str = execution_state.value(QStringLiteral("status")).toString().trimmed();
                if (status_str.isEmpty())
                    continue;

                TaskManager::TaskStatus status = taskManagerStatusFromName(status_str);
                if (status == TaskManager::Running || status == TaskManager::Stopping)
                    status = TaskManager::Failed;

                qint64 elapsed_sec = 0;
                if (execution_state.contains(QStringLiteral("elapsed_seconds")))
                    elapsed_sec = execution_state.value(QStringLiteral("elapsed_seconds")).toLongLong();
                else if (execution_state.contains(QStringLiteral("elapsed")))
                    elapsed_sec = parseDurationText(execution_state.value(QStringLiteral("elapsed")).toString());

                const int progress = execution_state.value(QStringLiteral("progress")).toInt();
                const QString phase = execution_state.value(QStringLiteral("phase")).toString();
                const QString run_id = execution_state.value(QStringLiteral("run_id")).toString();
                const QString config_path = task_db_path;
                const QString log_path = storage.testTaskLogPath(name, task_def.directory_name);

                task_manager_->restoreTask(uuid, name, ModelTaskType::Test,
                                           task_def.uuid, task_def.name, status, progress, elapsed_sec,
                                           phase, run_id, config_path, log_path);
            }
        }
    }
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
    else if (task_type == ModelTaskType::BoxToMask)
    {
        resolved_scope = QStringLiteral("box_to_mask");
        if (resolved_scope_name.isEmpty())
            resolved_scope_name = QStringLiteral("BoxToMask");
    }
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
        task_id = task_manager_->addTask(uuid, record.name, task_type, resolved_scope, resolved_scope_name);
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
    else if (task_type == ModelTaskType::BoxToMask)
    {
        config_path = storage.modelDatabasePath(record.name);
        log_path    = storage.testTaskLogPath(record.name, QStringLiteral("fs_sam2_box_to_mask"));
    }
    else if (has_resolved_definition)
    {
        config_path = storage.testTaskDatabasePath(record.name, resolved_definition.directory_name);
        log_path    = storage.testTaskLogPath(record.name, resolved_definition.directory_name);
    }
    task_manager_->setTaskPaths(task_id, config_path, log_path);
    return task_id;
}

bool ModelTaskController::prepareTask(const int task_id)
{
    if (shutting_down_ || task_manager_ == nullptr || model_manager_ == nullptr)
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
    // 训练/测试/评估任务的进度走模型任务面板（task 消息上报），不触发全局进度任务/ProgressDialog。
    options.manage_progress = false;

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
    const TaskIdentity task_identity = request_ptr->identity;
    const auto completion = [this, task_identity, process_spec](const dltool::data::DataOperationWorkflow::Result &result)
    {
        preparation_operations_.remove(task_identity);
        handlePreparedTask(task_identity, process_spec, result.success, result.error);
    };

    if (!describeModelTask(request_ptr->task_type).requires_dataset_export)
    {
        const auto operation = dltool::data::DataOperationWorkflow::start(
            this, std::move(options),
            [prepare](dltool::data::DataOperationWorkflow::Result &result) { prepare(nullptr, result); }, completion);
        if (operation != nullptr)
            preparation_operations_.insert(task_identity, operation);
        else if (const TaskManager::Task *current = task_manager_->findTask(task_id);
                 current != nullptr && current->status == TaskManager::Preparing)
        {
            failTask(task_id, QStringLiteral("无法提交模型任务后台准备"));
            return false;
        }
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
        const auto operation = data_manager_->runDatasetExportAsync(
            this, std::move(export_request), std::move(options),
            [prepare](const dltool::data::DatasetExportSource     &source,
                      dltool::data::DataOperationWorkflow::Result &result) { prepare(&source, result); },
            completion);
        if (operation != nullptr)
            preparation_operations_.insert(task_identity, operation);
        else if (const TaskManager::Task *current = task_manager_->findTask(task_id);
                 current != nullptr && current->status == TaskManager::Preparing)
        {
            failTask(task_id, QStringLiteral("无法提交模型任务数据准备"));
            return false;
        }
    }

    spdlog::info("模型任务进入后台准备, task_id: {}", task_id);
    return true;
}

bool ModelTaskController::stopTask(const int task_id)
{
    if (shutting_down_)
        return false;
    const bool stopped = task_manager_ != nullptr && task_manager_->stopTask(task_id);
    if (stopped)
        flushModelState(task_id);
    return stopped;
}

bool ModelTaskController::deleteTask(const int task_id)
{
    if (shutting_down_)
        return false;

    TaskIdentity identity;
    if (task_manager_ != nullptr)
    {
        if (const TaskManager::Task *task = task_manager_->findTask(task_id); task != nullptr)
            identity = task->identity;
    }

    const bool deleted = task_manager_ != nullptr && task_manager_->deleteTask(task_id);
    if (const auto operation = preparation_operations_.take(identity); operation != nullptr)
        operation->requestCancel();
    if (external_task_runner_ != nullptr && identity.isValid())
        external_task_runner_->deleteTask(identity);
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

    request.identity                        = task->identity;
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

void ModelTaskController::handlePreparedTask(const TaskIdentity                         &identity,
                                             const std::shared_ptr<ExternalProcessSpec> &process_spec,
                                             const bool success, const QString &error)
{
    if (shutting_down_ || task_manager_ == nullptr)
        return;

    const TaskManager::Task *task = task_manager_->findTask(identity.task_id);
    // 停止或删除发生在后台准备期间时，任务已不再是 Preparing，完成回调只需丢弃。
    if (task == nullptr || task->identity != identity || task->status != TaskManager::Preparing)
        return;

    if (!success)
    {
        failTask(identity.task_id, error.isEmpty() ? QString("准备模型任务失败") : error);
        return;
    }

    QString start_error;
    if (process_spec == nullptr || process_spec->identity != identity || external_task_runner_ == nullptr
        || !external_task_runner_->start(*process_spec, &start_error))
    {
        failTask(identity.task_id, start_error.isEmpty() ? QString("启动外部模型任务失败") : start_error);
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

bool ModelTaskController::verifyTaskArtifacts(const TaskManager::Task &task, QString *error_msg) const
{
    if (model_manager_ == nullptr)
        return setError(error_msg, QStringLiteral("模型管理器为空"));

    const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(task.model_uuid);
    if (!record.isValid())
        return setError(error_msg, QStringLiteral("模型不存在: %1").arg(task.model_uuid));

    const FrameworkDefinition framework = registeredFramework(method_, record.framework_name);
    const ModelStorageService storage(project_dir_);

    if (isTrainModelTask(task.type))
    {
        const QString weights_dir = storage.trainWeightsPath(record.name);
        if (weights_dir.isEmpty() || !QDir(weights_dir).exists())
            return setError(error_msg, QStringLiteral("模型训练未生成权重目录: %1").arg(weights_dir));

        const QFileInfoList entries = QDir(weights_dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        bool found_valid_weight = false;

        const QStringList allowed_extensions = framework.weight_extensions;
        if (allowed_extensions.isEmpty())
        {
            for (const QFileInfo &entry : entries)
            {
                if (entry.size() > 0)
                {
                    found_valid_weight = true;
                    break;
                }
            }
        }
        else
        {
            for (const QFileInfo &entry : entries)
            {
                if (entry.size() <= 0)
                    continue;
                const QString suffix = entry.suffix().trimmed();
                for (const QString &ext : allowed_extensions)
                {
                    const QString clean_ext = ext.startsWith(QLatin1Char('.')) ? ext.mid(1) : ext;
                    if (suffix.compare(clean_ext, Qt::CaseInsensitive) == 0)
                    {
                        found_valid_weight = true;
                        break;
                    }
                }
                if (found_valid_weight)
                    break;
            }
        }

        if (!found_valid_weight)
            return setError(error_msg, QStringLiteral("模型训练未生成有效权重文件 (目录: %1)").arg(weights_dir));

        return true;
    }

    if (isTestModelTask(task.type))
    {
        QString directory_name = task.scope_name.trimmed();
        if (directory_name.isEmpty() && !task.scope_uuid.trimmed().isEmpty())
        {
            ModelTestTaskDefinition definition;
            QString load_err;
            if (test_task_repository_.loadTask(record.name, task.scope_uuid, definition, &load_err))
                directory_name = definition.directory_name;
        }

        bool found_predictions = false;
        if (!directory_name.isEmpty())
        {
            // 优先检查本次任务在 staging 目录生成的预测产物
            const QString staging_pred_dir = storage.testTaskPredictionStagingPath(record.name, directory_name);
            if (!staging_pred_dir.isEmpty() && QDir(staging_pred_dir).exists())
            {
                const QFileInfoList entries = QDir(staging_pred_dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                for (const QFileInfo &entry : entries)
                {
                    if (entry.size() > 0)
                    {
                        found_predictions = true;
                        break;
                    }
                }
            }

            if (!found_predictions)
            {
                const QString staging_db_path = storage.testTaskDatabaseStagingPath(record.name, directory_name);
                if (!staging_db_path.isEmpty() && QFile::exists(staging_db_path))
                {
                    database::ModelTaskDataBase db(staging_db_path);
                    QHash<qint64, QVariant> predictions;
                    if (db.readPredictions(predictions) && !predictions.isEmpty())
                        found_predictions = true;
                }
            }

            // 回退检查 live 目录（兼容直接准备 live 产物的测试与历史数据）
            if (!found_predictions)
            {
                const QString pred_dir = storage.testTaskPredictionPath(record.name, directory_name);
                if (!pred_dir.isEmpty() && QDir(pred_dir).exists())
                {
                    const QFileInfoList entries = QDir(pred_dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                    for (const QFileInfo &entry : entries)
                    {
                        if (entry.size() > 0)
                        {
                            found_predictions = true;
                            break;
                        }
                    }
                }
            }

            if (!found_predictions)
            {
                const QString task_db_path = storage.testTaskDatabasePath(record.name, directory_name);
                if (!task_db_path.isEmpty() && QFile::exists(task_db_path))
                {
                    database::ModelTaskDataBase db(task_db_path);
                    QHash<qint64, QVariant> predictions;
                    if (db.readPredictions(predictions) && !predictions.isEmpty())
                        found_predictions = true;
                }
            }
        }

        if (!found_predictions)
        {
            const QString root_test_dir = storage.testRoot(record.name);
            const QString root_pred_dir = QDir(root_test_dir).filePath(QStringLiteral("pred"));
            if (!root_pred_dir.isEmpty() && QDir(root_pred_dir).exists())
            {
                const QFileInfoList entries = QDir(root_pred_dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                for (const QFileInfo &entry : entries)
                {
                    if (entry.size() > 0)
                    {
                        found_predictions = true;
                        break;
                    }
                }
            }
        }

        if (!found_predictions)
            return setError(error_msg, QStringLiteral("模型测试未生成有效预测结果"));

        return true;
    }

    return true;
}

bool ModelTaskController::publishTestTaskArtifacts(const QString &model_name, const QString &task_directory,
                                                   QString *err_msg) const
{
    const ModelStorageService storage(project_dir_);
    const QString staging_pred = storage.testTaskPredictionStagingPath(model_name, task_directory);
    const QString staging_db = storage.testTaskDatabaseStagingPath(model_name, task_directory);
    const QString live_pred = storage.testTaskPredictionPath(model_name, task_directory);
    const QString live_db = storage.testTaskDatabasePath(model_name, task_directory);
    const QString journal_path = storage.testTaskPublishJournalPath(model_name, task_directory);
    const QString task_root = storage.testTaskRoot(model_name, task_directory);
    const QString old_pred = task_root + QStringLiteral("/.old_pred");

    const bool has_staging_pred = !staging_pred.isEmpty() && QDir(staging_pred).exists();
    const bool has_staging_db = !staging_db.isEmpty() && QFile::exists(staging_db);
    if (!has_staging_pred && !has_staging_db)
        return true;

    // 1. 写入发布日志
    QFile journal(journal_path);
    if (journal.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("model_name"), model_name);
        obj.insert(QStringLiteral("task_directory"), task_directory);
        obj.insert(QStringLiteral("status"), QStringLiteral("publishing"));
        obj.insert(QStringLiteral("timestamp"), QDateTime::currentSecsSinceEpoch());
        journal.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        journal.close();
    }

    // 2. 提升预测结果文件目录
    if (has_staging_pred)
    {
        if (QDir(old_pred).exists())
            QDir(old_pred).removeRecursively();
        if (QDir(live_pred).exists())
        {
            if (!QDir().rename(live_pred, old_pred))
                QDir(live_pred).removeRecursively();
        }
        if (!QDir().rename(staging_pred, live_pred))
        {
            storage.copyDirectoryContents(staging_pred, live_pred, err_msg);
            QDir(staging_pred).removeRecursively();
        }
        if (QDir(old_pred).exists())
            QDir(old_pred).removeRecursively();
    }

    // 3. 原子发布数据库记录
    if (has_staging_db)
    {
        QHash<qint64, QVariant> staging_predictions;
        QVariantMap staging_prep;
        {
            database::ModelTaskDataBase staging_task_db(staging_db);
            if (!staging_task_db.readPredictions(staging_predictions, err_msg))
                return false;

            staging_task_db.readPreprocessingConfig(staging_prep);
        }

        database::ModelTaskDataBase live_task_db(live_db);
        if (!live_task_db.replacePredictions(staging_predictions, err_msg))
            return false;

        if (!staging_prep.isEmpty())
        {
            if (!live_task_db.writePreprocessingConfig(staging_prep, err_msg))
                return false;
        }
        QFile::remove(staging_db);
    }

    // 4. 清理发布日志
    if (QFile::exists(journal_path))
        QFile::remove(journal_path);

    return true;
}

bool ModelTaskController::discardTestTaskStaging(const QString &model_name, const QString &task_directory,
                                                 QString *err_msg) const
{
    const ModelStorageService storage(project_dir_);
    const QString staging_pred = storage.testTaskPredictionStagingPath(model_name, task_directory);
    const QString staging_db = storage.testTaskDatabaseStagingPath(model_name, task_directory);
    const QString journal_path = storage.testTaskPublishJournalPath(model_name, task_directory);
    const QString old_pred = storage.testTaskRoot(model_name, task_directory) + QStringLiteral("/.old_pred");

    if (QDir(staging_pred).exists() && !QDir(staging_pred).removeRecursively())
        return setError(err_msg, QStringLiteral("清理临时预测目录失败"));
    if (QFile::exists(staging_db) && !QFile::remove(staging_db))
        return setError(err_msg, QStringLiteral("清理临时任务数据库失败"));
    if (QDir(old_pred).exists() && !QDir(old_pred).removeRecursively())
        return setError(err_msg, QStringLiteral("清理备份预测目录失败"));
    if (QFile::exists(journal_path) && !QFile::remove(journal_path))
        return setError(err_msg, QStringLiteral("清理发布日志失败"));
    return true;
}

bool ModelTaskController::recoverTestTaskPublish(const QString &model_name, const QString &task_directory,
                                                QString *err_msg) const
{
    const ModelStorageService storage(project_dir_);
    const QString staging_pred = storage.testTaskPredictionStagingPath(model_name, task_directory);
    const QString staging_db = storage.testTaskDatabaseStagingPath(model_name, task_directory);
    const QString live_pred = storage.testTaskPredictionPath(model_name, task_directory);
    const QString journal_path = storage.testTaskPublishJournalPath(model_name, task_directory);
    const QString old_pred = storage.testTaskRoot(model_name, task_directory) + QStringLiteral("/.old_pred");

    const bool has_journal = QFile::exists(journal_path);
    const bool has_staging_pred = !staging_pred.isEmpty() && QDir(staging_pred).exists();
    const bool has_staging_db = !staging_db.isEmpty() && QFile::exists(staging_db);

    if (has_journal)
    {
        if (has_staging_pred || has_staging_db)
        {
            if (!publishTestTaskArtifacts(model_name, task_directory, err_msg))
                return false;
        }
        else if (QDir(old_pred).exists())
        {
            if (!QDir(live_pred).exists())
                QDir().rename(old_pred, live_pred);
            else
                QDir(old_pred).removeRecursively();
        }
        if (QFile::exists(journal_path))
            QFile::remove(journal_path);
    }
    else
    {
        if (has_staging_pred || has_staging_db || QDir(old_pred).exists())
            discardTestTaskStaging(model_name, task_directory, err_msg);
    }
    return true;
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
    else if (task.status == TaskManager::Running || task.status == TaskManager::Stopping)
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
    if (task == nullptr || (!isTrainModelTask(task->type) && !isTestModelTask(task->type) && task->type != ModelTaskType::BoxToMask))
    {
        pending_extra_updates_.remove(task_id);
        return;
    }

    // 取出本次待合并的指标字段（epoch/iter/lr/loss/eta/metrics/
    // message/status）。progress 与 started 不在此缓冲，由状态投影写入。
    QVariantMap updates = pending_extra_updates_.take(task_id);

    const bool train_scope = isTrainModelTask(task->type);
    const bool b2m_scope   = (task->type == ModelTaskType::BoxToMask);
    // 小样本测试任务（框架能力位 few_shot）没有 UUID 测试任务记录，其状态
    // 投影到 extra_data.test 顶层 section；能力来源为框架注册表。
    const bool legacy_few_shot_test
        = isTestModelTask(task->type) && task->scope_uuid.trimmed().isEmpty()
       && registeredFramework(method_, model_manager_->modelRecordViewForUuid(task->model_uuid).framework_name)
              .isFewShot();

    // 权威来源是 TaskManager 的本地时钟。
    // 写入 extra_data / task.db 链路，保存数值耗时与运行身份，保证项目重开后可以恢复显示与终态。
    updates.insert(QStringLiteral("elapsed"), task_manager_->taskRunningTime(task_id));
    updates.insert(QStringLiteral("elapsed_seconds"), task_manager_->taskRunningTimeSeconds(task_id));
    if (task->identity.isValid())
    {
        updates.insert(QStringLiteral("run_id"), task->identity.run_id);
        updates.insert(QStringLiteral("project_id"), task->identity.project_id);
        updates.insert(QStringLiteral("task_id"), task->identity.task_id);
    }

    if (!train_scope && !b2m_scope && !legacy_few_shot_test)
    {
        const ModelManager::ModelRecordView record = model_manager_->modelRecordViewForUuid(task->model_uuid);
        if (!record.name.isEmpty())
        {
            ModelTestTaskDefinition task_def;
            if (test_task_repository_.loadTask(record.name, task->scope_uuid, task_def))
            {
                const QString task_db_path = ModelStorageService(project_dir_).testTaskDatabasePath(record.name, task_def.directory_name);
                database::ModelTaskDataBase task_db(task_db_path);
                QVariantMap execution_state;
                task_db.readExecutionState(execution_state);
                for (auto it = updates.cbegin(); it != updates.cend(); ++it)
                {
                    execution_state.insert(it.key(), it.value());
                }
                applyTaskStateToSection(*task, TaskManager::isTerminal(task->status),
                                        task->status == TaskManager::Finished, execution_state);
                QString error;
                if (!task_db.writeExecutionState(execution_state, &error))
                {
                    spdlog::error("保存测试任务执行状态失败, task_id: {}, uuid: {}, 错误: {}", task_id,
                                  task->scope_uuid.toUtf8().constData(), error.toUtf8().constData());
                }
            }
        }
        return;
    }

    const QVariantMap current_model = model_manager_->modelRecordForUuid(task->model_uuid);
    const QVariantMap extra_data    = current_model.value(QStringLiteral("extra_data")).toMap();
    QVariantMap       section;
    if (train_scope)
        section = extra_data.value(QStringLiteral("train")).toMap();
    else if (b2m_scope)
        section = extra_data.value(QStringLiteral("box_to_mask")).toMap();
    else if (legacy_few_shot_test)
        section = extra_data.value(QStringLiteral("test")).toMap();

    for (auto it = updates.cbegin(); it != updates.cend(); ++it) section.insert(it.key(), it.value());

    // 状态投影：进度/开始标记以任务中心为准，与 TaskManager 表格逐帧一致。
    applyTaskStateToSection(*task, TaskManager::isTerminal(task->status), task->status == TaskManager::Finished,
                            section);

    QString     error;
    QVariantMap state_update;
    if (train_scope)
        state_update.insert(QStringLiteral("train"), section);
    else if (b2m_scope)
        state_update.insert(QStringLiteral("box_to_mask"), section);
    else if (legacy_few_shot_test)
        state_update.insert(QStringLiteral("test"), section);

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

void ModelTaskController::handleTaskStartRequested(const TaskIdentity &identity)
{
    if (shutting_down_ || task_manager_ == nullptr)
        return;
    const TaskManager::Task *task = task_manager_->findTask(identity.task_id);
    if (task != nullptr && task->identity == identity && taskBelongsToCurrentModelManager(identity.task_id))
        prepareTask(identity.task_id);
}

void ModelTaskController::handleTaskMessage(const TaskMessage &message)
{
    if (shutting_down_ || model_manager_ == nullptr || task_manager_ == nullptr || !message.identity.isValid()
        || message.type == TaskMessageType::Log || message.type == TaskMessageType::Command)
    {
        return;
    }

    const TaskManager::Task *task = task_manager_->findTask(message.identity.task_id);
    if (task == nullptr || task->identity != message.identity || TaskManager::isTerminal(task->status)
        || (!isTrainModelTask(task->type) && !isTestModelTask(task->type) && task->type != ModelTaskType::BoxToMask))
        return;

    // 高频进度消息只合并进内存缓冲，由节流定时器统一落库（≤1 次/秒/任务）。
    // TaskManager 表格仍由 TaskManager 每事件即时更新（纯内存，无磁盘 IO）。
    // 消息只携带指标字段；progress/started 一律由任务状态投影写入（见
    // applyTaskStateToSection），不在此处维护独立进度值。
    // 耗时由本地 TaskManager 时钟权威计算，避免 Python payload 中的 elapsed 平行覆盖。
    QVariantMap &pending = pending_extra_updates_[message.identity.task_id];
    for (const QString &key : {QStringLiteral("epoch"), QStringLiteral("iter"), QStringLiteral("lr"),
                               QStringLiteral("loss"), QStringLiteral("eta")})
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
        touchTaskModelModifiedTime(message.identity.task_id);
        flushModelState(message.identity.task_id);
        return;
    }

    if (!extra_flush_timer_->isActive())
        extra_flush_timer_->start();
}

void ModelTaskController::handleTaskRunningTimeChanged(const int task_id)
{
    if (shutting_down_ || task_manager_ == nullptr || !taskBelongsToCurrentModelManager(task_id))
        return;

    const TaskManager::Task *task = task_manager_->findTask(task_id);
    if (task == nullptr)
        return;

    const bool train_scope = isTrainModelTask(task->type);
    const bool b2m_scope   = (task->type == ModelTaskType::BoxToMask);
    const bool legacy_few_shot_test
        = isTestModelTask(task->type) && task->scope_uuid.trimmed().isEmpty()
       && registeredFramework(method_, model_manager_->modelRecordViewForUuid(task->model_uuid).framework_name)
              .isFewShot();
    const bool test_scope  = isTestModelTask(task->type);

    if (!train_scope && !b2m_scope && !legacy_few_shot_test && !test_scope)
        return;

    if (TaskManager::isTerminal(task->status))
    {
        flushModelState(task_id);
        return;
    }

    // 复用 Python 状态更新的缓冲和节流写库路径；没有 Python 消息时，
    // 本地时钟也会定期把 elapsed 与 elapsed_seconds 写入模型数据库。
    pending_extra_updates_[task_id].insert(QStringLiteral("elapsed"),
                                           task_manager_->taskRunningTime(task_id));
    pending_extra_updates_[task_id].insert(QStringLiteral("elapsed_seconds"),
                                           task_manager_->taskRunningTimeSeconds(task_id));
    if (!extra_flush_timer_->isActive())
        extra_flush_timer_->start();
}

void ModelTaskController::handleTaskStopRequested(const TaskIdentity &identity)
{
    if (!taskBelongsToCurrentModelManager(identity.task_id))
        return;

    const TaskManager::Task *task = task_manager_ != nullptr ? task_manager_->findTask(identity.task_id) : nullptr;
    if (task == nullptr || task->identity != identity)
        return;

    if (const auto operation = preparation_operations_.value(identity); operation != nullptr)
        operation->requestCancel();

    if (external_task_runner_ != nullptr && external_task_runner_->hasRunningTask(identity))
    {
        external_task_runner_->stop(identity);
        return;
    }

    const ModelManager::ModelRecordView record    = model_manager_->modelRecordViewForUuid(task->model_uuid);
    const FrameworkDefinition           framework = registeredFramework(method_, record.framework_name);
    if (framework.supportsExternalTask(task->type))
    {
        // 外部任务必须等待实际执行者进程退出并由 handleExternalTaskFinished 发布终态，此处不直接 markTaskStopped。
        return;
    }

    if (task_manager_ != nullptr)
        task_manager_->markTaskStopped(identity.task_id);
    syncTaskModelState(identity.task_id);
    touchTaskModelModifiedTime(identity.task_id);
}

void ModelTaskController::handleExternalTaskStarted(const TaskIdentity &identity)
{
    if (shutting_down_ || task_manager_ == nullptr || !taskBelongsToCurrentModelManager(identity.task_id))
        return;

    const TaskManager::Task *task = task_manager_->findTask(identity.task_id);
    if (task == nullptr || task->identity != identity)
        return;

    if (task->status == TaskManager::Preparing)
    {
        if (task_manager_->markTaskRunning(identity.task_id))
        {
            syncTaskModelState(identity.task_id);
            touchTaskModelModifiedTime(identity.task_id);
        }
        return;
    }

    // 用户在 QProcess::Starting 阶段点击停止时，进程刚启动也必须继续收敛。
    if (task->status == TaskManager::Stopping && external_task_runner_ != nullptr)
        external_task_runner_->stop(identity);
}

void ModelTaskController::handleExternalTaskStartFailed(const TaskIdentity &identity, const QString &error)
{
    if (shutting_down_ || task_manager_ == nullptr || !taskBelongsToCurrentModelManager(identity.task_id))
        return;

    const TaskManager::Task *task = task_manager_->findTask(identity.task_id);
    if (task == nullptr || task->identity != identity)
        return;

    if (task->status == TaskManager::Stopping)
    {
        task_manager_->markTaskStopped(identity.task_id);
        syncTaskModelState(identity.task_id);
        touchTaskModelModifiedTime(identity.task_id);
        return;
    }
    if (task->status == TaskManager::Preparing || task->status == TaskManager::Running)
        failTask(identity.task_id, error.isEmpty() ? QString("外部模型任务进程启动失败") : error);
}

void ModelTaskController::handleExternalTaskFinished(const TaskIdentity &identity, const int exit_code,
                                                     const bool normal_exit,
                                                     const bool stop_requested)
{
    if (shutting_down_ || task_manager_ == nullptr || !taskBelongsToCurrentModelManager(identity.task_id))
        return;

    const TaskManager::Task *task = task_manager_->findTask(identity.task_id);
    if (task == nullptr || task->identity != identity)
        return;
    if (TaskManager::isTerminal(task->status))
    {
        syncTaskModelState(identity.task_id);
        return;
    }

    touchTaskModelModifiedTime(identity.task_id);
    const QString model_name = model_manager_ != nullptr ? model_manager_->modelRecordViewForUuid(task->model_uuid).name : QString();
    QString test_directory_name;
    if (isTestModelTask(task->type))
    {
        test_directory_name = task->scope_name.trimmed();
        if (test_directory_name.isEmpty() && !task->scope_uuid.trimmed().isEmpty() && !model_name.isEmpty())
        {
            ModelTestTaskDefinition definition;
            QString load_err;
            if (test_task_repository_.loadTask(model_name, task->scope_uuid, definition, &load_err))
                test_directory_name = definition.directory_name;
        }
    }

    if (task->status == TaskManager::Stopping || stop_requested || (normal_exit && exit_code == 2))
    {
        if (isTestModelTask(task->type) && !model_name.isEmpty() && !test_directory_name.isEmpty())
            discardTestTaskStaging(model_name, test_directory_name);
        task_manager_->markTaskStopped(identity.task_id);
        syncTaskModelState(identity.task_id);
        return;
    }
    if (normal_exit && exit_code == 0)
    {
        QString artifact_error;
        if (!verifyTaskArtifacts(*task, &artifact_error))
        {
            if (isTestModelTask(task->type) && !model_name.isEmpty() && !test_directory_name.isEmpty())
                discardTestTaskStaging(model_name, test_directory_name);
            failTask(identity.task_id, artifact_error);
            return;
        }

        if (isTestModelTask(task->type) && !model_name.isEmpty() && !test_directory_name.isEmpty())
        {
            QString publish_err;
            if (!publishTestTaskArtifacts(model_name, test_directory_name, &publish_err))
            {
                discardTestTaskStaging(model_name, test_directory_name);
                failTask(identity.task_id, QString("发布测试预测产物失败: %1").arg(publish_err));
                return;
            }
        }

        if (task->status == TaskManager::Preparing)
            task_manager_->markTaskRunning(identity.task_id);
        task_manager_->updateTaskPhase(identity.task_id, QStringLiteral("finished"));
        task_manager_->finishTask(identity.task_id);
        syncTaskModelState(identity.task_id);
        return;
    }

    task = task_manager_->findTask(identity.task_id);
    if (task != nullptr && !TaskManager::isTerminal(task->status))
    {
        if (isTestModelTask(task->type) && !model_name.isEmpty() && !test_directory_name.isEmpty())
            discardTestTaskStaging(model_name, test_directory_name);
        const QString name = modelTaskDisplayName(task->type);
        failTask(identity.task_id, normal_exit ? QString("%1失败（退出码 %2），请查看模型日志。").arg(name).arg(exit_code)
                                      : QString("%1异常退出，请查看模型日志。").arg(name));
    }
}

} // namespace dltool::model
