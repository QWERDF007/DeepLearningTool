#include "model/TaskManager.h"

#include "model/TaskCommunication.h"
#include "ui/SignalHelper.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <algorithm>
#include <utility>

namespace dltool::model {

namespace {

constexpr int kRuntimeRefreshIntervalMs = 1000;

int boundedProgress(const int progress)
{
    return std::clamp(progress, 0, 100);
}

QString durationText(qint64 seconds)
{
    seconds            = std::max<qint64>(0, seconds);
    const qint64 hours = seconds / 3600;
    seconds %= 3600;
    const qint64 minutes = seconds / 60;
    seconds %= 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace

TaskManager::TaskManager(QObject *parent)
    : QAbstractTableModel(parent)
    , runtime_timer_(new QTimer(this))
    , communication_server_(new TaskCommunicationServer(this))
{
    runtime_timer_->setInterval(kRuntimeRefreshIntervalMs);
    connect(runtime_timer_, &QTimer::timeout, this, &TaskManager::refreshRunningTasks);
    runtime_timer_->start();
    connect(communication_server_, &TaskCommunicationServer::messageReceived, this, &TaskManager::handleTaskMessage);
}

int TaskManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(tasks_.size());
}

int TaskManager::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

int TaskManager::count() const
{
    return rowCount();
}

int TaskManager::revision() const
{
    return revision_;
}

QVariant TaskManager::data(const QModelIndex &index, const int role) const
{
    if (index.row() < 0 || index.row() >= rowCount() || index.column() < 0 || index.column() >= columnCount())
        return {};

    const Task &task = tasks_.at(static_cast<size_t>(index.row()));
    switch (role)
    {
    case Qt::DisplayRole:
        return dataForColumn(task, index.column());
    case TaskIdRole:
        return task.id;
    case ModelUuidRole:
        return task.model_uuid;
    case ModelNameRole:
        return task.model_name;
    case ScopeUuidRole:
        return task.scope_uuid;
    case ScopeNameRole:
        return task.scope_name;
    case DisplayNameRole:
        return task.model_name;
    case TaskTypeRole:
        return static_cast<int>(task.type);
    case TaskTypeTextRole:
        return modelTaskDisplayName(task.type);
    case StatusRole:
        return statusText(task);
    case StatusValueRole:
        return task.status;
    case CreatedAtRole:
        return createdAtText(task);
    case RunningTimeRole:
        return runningTimeText(task);
    case EtaRole:
        return etaText(task);
    case ProgressRole:
        return task.progress;
    case CanStartRole:
        return canStart(task);
    case CanPauseRole:
        return canPause(task);
    case CanStopRole:
        return canStop(task);
    case CanFinishRole:
        return canFinish(task);
    case CanDeleteRole:
        return canDelete(task);
    case PhaseRole:
        return task.phase;
    case ConfigPathRole:
        return task.config_path;
    case LogPathRole:
        return task.log_path;
    default:
        return {};
    }
}

QVariant TaskManager::headerData(const int section, const Qt::Orientation orientation, const int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section)
    {
    case TaskIdColumn:
        return QString("任务ID");
    case ModelNameColumn:
        return QString("模型名称");
    case TaskTypeColumn:
        return QString("任务类型");
    case StatusColumn:
        return QString("任务状态");
    case CreatedAtColumn:
        return QString("任务创建时间");
    case RunningTimeColumn:
        return QString("运行时间");
    case EtaColumn:
        return QString("剩余时间");
    case ProgressColumn:
        return QString("进度");
    case ActionsColumn:
        return QString("操作");
    default:
        return {};
    }
}

QHash<int, QByteArray> TaskManager::roleNames() const
{
    return {
        { Qt::DisplayRole,        "display"},
        {      TaskIdRole,        "task_id"},
        {   ModelUuidRole,     "model_uuid"},
        {   ModelNameRole,     "model_name"},
        {   ScopeUuidRole,     "scope_uuid"},
        {   ScopeNameRole,     "scope_name"},
        { DisplayNameRole,   "display_name"},
        {    TaskTypeRole,      "task_type"},
        {TaskTypeTextRole, "task_type_text"},
        {      StatusRole,         "status"},
        { StatusValueRole,   "status_value"},
        {   CreatedAtRole,     "created_at"},
        { RunningTimeRole,   "running_time"},
        {         EtaRole,            "eta"},
        {    ProgressRole,       "progress"},
        {    CanStartRole,      "can_start"},
        {    CanPauseRole,      "can_pause"},
        {     CanStopRole,       "can_stop"},
        {   CanFinishRole,     "can_finish"},
        {   CanDeleteRole,     "can_delete"},
        {       PhaseRole,          "phase"},
        {  ConfigPathRole,    "config_path"},
        {     LogPathRole,       "log_path"},
    };
}

int TaskManager::addTask(const QString &model_uuid, const QString &model_name, const ModelTaskType task_type)
{
    return addTask(model_uuid, model_name, task_type, true);
}

int TaskManager::addTask(const QString &model_uuid, const QString &model_name, const ModelTaskType task_type,
                         const bool supports_pause)
{
    const QString scope_uuid = isTrainModelTask(task_type) ? QStringLiteral("train") : QString();
    return addTask(model_uuid, model_name, task_type, scope_uuid, {}, supports_pause);
}

int TaskManager::addTask(const QString &model_uuid, const QString &model_name, const ModelTaskType task_type,
                         const QString &scope_uuid, const QString &scope_name, const bool supports_pause)
{
    const QString uuid = model_uuid.trimmed();
    const QString name = model_name.trimmed();
    if (uuid.isEmpty() || name.isEmpty() || !isKnownModelTask(task_type))
        return -1;

    const int row = rowCount();
    beginInsertRows({}, row, row);
    const int task_id = next_task_id_++;
    Task      task;
    task.id         = task_id;
    task.model_uuid = uuid;
    task.model_name = name;
    task.scope_uuid = scope_uuid.trimmed();
    task.scope_name = scope_name.trimmed();
    // Task type and test-task scope are already shown in their own columns;
    // keep the model-name column identical for training and testing.
    task.display_name   = name;
    task.type           = task_type;
    task.status         = Pending;
    task.created_at     = QDateTime::currentSecsSinceEpoch();
    task.eta_seconds    = -1;
    task.supports_pause = supports_pause;
    tasks_.push_back(std::move(task));
    endInsertRows();
    emit countChanged();
    ++revision_;
    emit revisionChanged();
    spdlog::info("添加任务, task_id: {}, 模型: {}, 类型: {}", task_id, name.toUtf8().constData(),
                 modelTaskKey(task_type).toUtf8().constData());
    return task_id;
}

bool TaskManager::setTaskPaths(const int task_id, const QString &config_path, const QString &log_path)
{
    const int row = rowForTask(task_id);
    if (row < 0)
        return false;

    Task         &task              = tasks_[static_cast<size_t>(row)];
    const QString normalized_config = config_path.trimmed();
    const QString normalized_log    = log_path.trimmed();
    if (task.config_path == normalized_config && task.log_path == normalized_log)
        return true;

    task.config_path = normalized_config;
    task.log_path    = normalized_log;
    emitTaskChanged(row, {ConfigPathRole, LogPathRole});
    return true;
}

bool TaskManager::startTask(const int task_id)
{
    const Task *task = findTask(task_id);
    if (task == nullptr || !canStart(*task))
        return false;

    // 两个 UI 入口都先进入 Preparing，再由所属控制器提交后台准备工作。
    if (!setTaskStatus(task_id, Preparing))
        return false;

    emit taskStartRequested(task_id);
    return true;
}

bool TaskManager::pauseTask(const int task_id)
{
    const Task *task = findTask(task_id);
    return task != nullptr && canPause(*task) && setTaskStatus(task_id, Paused);
}

bool TaskManager::stopTask(const int task_id)
{
    const Task *task = findTask(task_id);
    if (task == nullptr || !canStop(*task))
        return false;

    if (!setTaskStatus(task_id, Stopping))
        return false;

    const bool command_sent
        = communication_server_ != nullptr && communication_server_->sendCommand(task_id, TaskCommand::Stop);
    emit taskStopRequested(task_id);
    spdlog::info("停止任务, task_id: {}, 通信命令: {}", task_id, command_sent ? "已发送" : "未发送");
    return true;
}

bool TaskManager::finishTask(const int task_id)
{
    const Task *task = findTask(task_id);
    return task != nullptr && canFinish(*task) && setTaskStatus(task_id, Finished);
}

bool TaskManager::failTask(const int task_id)
{
    const Task *task = findTask(task_id);
    return task != nullptr && !isTerminal(task->status) && task->status != Stopping && setTaskStatus(task_id, Failed);
}

bool TaskManager::markTaskRunning(const int task_id)
{
    const Task *task = findTask(task_id);
    return task != nullptr && (task->status == Pending || task->status == Preparing || task->status == Paused)
        && setTaskStatus(task_id, Running);
}

bool TaskManager::markTaskStopped(const int task_id)
{
    const Task *task = findTask(task_id);
    return task != nullptr && (!isTerminal(task->status) || task->status == Stopped) && setTaskStatus(task_id, Stopped);
}

bool TaskManager::deleteTask(const int task_id)
{
    const int row = rowForTask(task_id);
    if (row < 0)
        return false;

    const Task &task = tasks_.at(static_cast<size_t>(row));
    // Keep active records routable until their process/background preparation
    // has converged.  Deleting one here would let late events become orphaned
    // and could mix them with a later task using the same scope.
    if (!canDelete(task))
        return false;

    beginRemoveRows({}, row, row);
    tasks_.erase(tasks_.begin() + row);
    endRemoveRows();
    emit countChanged();
    ++revision_;
    emit revisionChanged();
    return true;
}

bool TaskManager::updateTaskProgress(const int task_id, const int progress)
{
    const int row = rowForTask(task_id);
    if (row < 0)
        return false;

    Task &task = tasks_[static_cast<size_t>(row)];
    if (isTerminal(task.status))
        return false;

    const int  bounded     = boundedProgress(progress);
    const bool eta_changed = bounded >= 100 && task.eta_seconds != 0;
    if (task.progress == bounded && !eta_changed)
        return true;

    task.progress = bounded;
    if (eta_changed)
    {
        task.eta_seconds = 0;
        emit dataChanged(index(row, EtaColumn), index(row, ProgressColumn), {EtaRole, ProgressRole, Qt::DisplayRole});
    }
    else
    {
        emit dataChanged(index(row, ProgressColumn), index(row, ProgressColumn), {ProgressRole, Qt::DisplayRole});
    }
    return true;
}

bool TaskManager::updateTaskPhase(const int task_id, const QString &phase)
{
    const int row = rowForTask(task_id);
    if (row < 0)
        return false;
    Task         &task  = tasks_[static_cast<size_t>(row)];
    const QString value = phase.trimmed();
    if (task.phase == value)
        return true;
    task.phase = value;
    emitTaskChanged(row, {PhaseRole});
    return true;
}

bool TaskManager::updateTaskEta(const int task_id, const qint64 eta_seconds)
{
    const int row = rowForTask(task_id);
    if (row < 0)
        return false;

    Task        &task  = tasks_[static_cast<size_t>(row)];
    const qint64 value = eta_seconds < 0 ? -1 : eta_seconds;
    if (task.eta_seconds == value)
        return true;

    task.eta_seconds = value;
    emit dataChanged(index(row, EtaColumn), index(row, EtaColumn), {EtaRole, Qt::DisplayRole});
    return true;
}

void TaskManager::clearTasks()
{
    if (tasks_.empty())
        return;

    beginResetModel();
    tasks_.clear();
    next_task_id_ = 1;
    endResetModel();
    emit countChanged();
    ++revision_;
    emit revisionChanged();
}

int TaskManager::findModelTask(const QString &model_uuid, const ModelTaskType task_type,
                               const bool include_finished) const
{
    const QString scope_uuid = isTrainModelTask(task_type) ? QStringLiteral("train") : QString();
    const Task   *task       = findModelTaskRecord(model_uuid, task_type, scope_uuid, include_finished);
    return task != nullptr ? task->id : -1;
}

int TaskManager::findModelTask(const QString &model_uuid, const ModelTaskType task_type, const QString &scope_uuid,
                               const bool include_finished) const
{
    const Task *task = findModelTaskRecord(model_uuid, task_type, scope_uuid, include_finished);
    return task != nullptr ? task->id : -1;
}

const TaskManager::Task *TaskManager::findTask(const int task_id) const
{
    const int row = rowForTask(task_id);
    return row >= 0 ? &tasks_.at(static_cast<size_t>(row)) : nullptr;
}

bool TaskManager::hasActiveModelTasks(const QString &model_uuid) const
{
    const QString value = model_uuid.trimmed();
    if (value.isEmpty())
        return false;
    return std::any_of(tasks_.cbegin(), tasks_.cend(),
                       [&value](const Task &task)
                       {
                           if (task.model_uuid != value)
                               return false;
                           return task.status == Preparing || task.status == Running || task.status == Paused
                               || task.status == Stopping;
                       });
}

const TaskManager::Task *TaskManager::findModelTaskRecord(const QString &model_uuid, const ModelTaskType task_type,
                                                          const bool include_finished) const
{
    const QString scope_uuid = isTrainModelTask(task_type) ? QStringLiteral("train") : QString();
    const int     row        = rowForModelTask(model_uuid.trimmed(), task_type, scope_uuid, include_finished);
    return row >= 0 ? &tasks_.at(static_cast<size_t>(row)) : nullptr;
}

const TaskManager::Task *TaskManager::findModelTaskRecord(const QString &model_uuid, const ModelTaskType task_type,
                                                          const QString &scope_uuid, const bool include_finished) const
{
    const int row = rowForModelTask(model_uuid.trimmed(), task_type, scope_uuid.trimmed(), include_finished);
    return row >= 0 ? &tasks_.at(static_cast<size_t>(row)) : nullptr;
}

bool TaskManager::canStartTask(const int task_id) const
{
    const Task *task = findTask(task_id);
    return task != nullptr && canStart(*task);
}

bool TaskManager::canPauseTask(const int task_id) const
{
    const Task *task = findTask(task_id);
    return task != nullptr && canPause(*task);
}

bool TaskManager::canStopTask(const int task_id) const
{
    const Task *task = findTask(task_id);
    return task != nullptr && canStop(*task);
}

bool TaskManager::canDeleteTask(const int task_id) const
{
    const Task *task = findTask(task_id);
    return task != nullptr && canDelete(*task);
}

bool TaskManager::isTerminal(const TaskStatus status)
{
    return status == Stopped || status == Finished || status == Failed;
}

bool TaskManager::ensureTaskServer(QString *err_msg)
{
    return communication_server_ != nullptr && communication_server_->start(err_msg);
}

QString TaskManager::taskServerHost() const
{
    return communication_server_ != nullptr ? communication_server_->host() : QStringLiteral("127.0.0.1");
}

quint16 TaskManager::taskServerPort() const
{
    return communication_server_ != nullptr ? communication_server_->port() : 0;
}

void TaskManager::handleTaskMessage(const TaskMessage &message)
{
    if (message.task_id < 0)
        return;

    if (message.type == TaskMessageType::Log)
    {
        const QString error_prefix = QStringLiteral("[DLTOOL_ERROR] ");
        if (message.message.startsWith(error_prefix))
            spdlog::error("任务 {} 失败详情: {}", message.task_id,
                          message.message.sliced(error_prefix.size()).toUtf8().constData());
        else if (!message.message.isEmpty())
            spdlog::info("任务 {}: {}", message.task_id, message.message.toUtf8().constData());
        emit taskMessageReceived(message);
        return;
    }

    if (findTask(message.task_id) == nullptr)
        return;

    const Task *task = findTask(message.task_id);
    if (task == nullptr)
        return;

    // Python 退出后可能仍有缓冲区中的迟到事件。状态不能被它们重新打开，
    // 但仍转发给控制器，以便其处理最后一条结果 payload。
    if (isTerminal(task->status))
    {
        emit taskMessageReceived(message);
        return;
    }

    // 用户已请求停止时，以停止为最终结果，不让脚本的失败/完成上报覆盖它。
    if (task->status == Stopping)
    {
        if (message.status == TaskProtocolStatus::Stopped || message.status == TaskProtocolStatus::Finished
            || message.status == TaskProtocolStatus::Failed || message.status == TaskProtocolStatus::Error)
        {
            markTaskStopped(message.task_id);
        }
        emit taskMessageReceived(message);
        return;
    }

    if (message.progress >= 0)
        updateTaskProgress(message.task_id, message.progress);
    if (message.payload.contains(taskProtocolFieldName(TaskProtocolField::EtaSeconds)) && message.eta_seconds >= 0)
        updateTaskEta(message.task_id, message.eta_seconds);
    if (message.payload.contains(QStringLiteral("phase")))
        updateTaskPhase(message.task_id, message.payload.value(QStringLiteral("phase")).toString());

    switch (message.status)
    {
    case TaskProtocolStatus::Running:
        markTaskRunning(message.task_id);
        break;
    case TaskProtocolStatus::Paused:
        if (const Task *current = findTask(message.task_id); current != nullptr && current->status == Running)
            setTaskStatus(message.task_id, Paused);
        break;
    case TaskProtocolStatus::Stopped:
        markTaskStopped(message.task_id);
        break;
    case TaskProtocolStatus::Finished:
        // A test runner finishes after inference.  The selected test page
        // starts the lazy in-memory C++ evaluation from this terminal state.
        if (task->type != ModelTaskType::Test)
            finishTask(message.task_id);
        break;
    case TaskProtocolStatus::Failed:
    case TaskProtocolStatus::Error:
        if (failTask(message.task_id))
        {
            spdlog::error("任务 {} 失败: {}", message.task_id, message.message.toUtf8().constData());
            ui::SignalHelper::notifyError(
                QString("模型任务 %1 失败").arg(message.task_id),
                message.message.isEmpty() ? QString("任务执行失败，请查看模型日志。") : message.message);
        }
        break;
    default:
        break;
    }

    emit taskMessageReceived(message);
}

int TaskManager::rowForTask(const int task_id) const
{
    for (int row = 0; row < static_cast<int>(tasks_.size()); ++row)
    {
        if (tasks_[static_cast<size_t>(row)].id == task_id)
            return row;
    }
    return -1;
}

int TaskManager::rowForModelTask(const QString &model_uuid, const ModelTaskType task_type, const QString &scope_uuid,
                                 const bool include_finished) const
{
    if (model_uuid.isEmpty() || !isKnownModelTask(task_type))
        return -1;

    for (int row = static_cast<int>(tasks_.size()) - 1; row >= 0; --row)
    {
        const Task &task = tasks_.at(static_cast<size_t>(row));
        if (task.model_uuid == model_uuid && task.type == task_type && task.scope_uuid == scope_uuid
            && (include_finished || task.status != Finished))
            return row;
    }
    return -1;
}

bool TaskManager::setTaskStatus(const int task_id, const TaskStatus status)
{
    const int row = rowForTask(task_id);
    if (row < 0)
        return false;

    Task &task = tasks_[static_cast<size_t>(row)];
    if (task.status == status)
        return true;

    const TaskStatus previous_status = task.status;
    const qint64     now             = QDateTime::currentSecsSinceEpoch();
    const bool       was_counting    = (task.status == Running || task.status == Stopping) && task.started_at > 0;
    const bool       will_count      = status == Running || status == Stopping;
    if (was_counting && !will_count)
        task.elapsed_seconds += std::max<qint64>(0, now - task.started_at);

    if (status == Preparing)
    {
        if (previous_status == Stopped || previous_status == Failed)
        {
            task.elapsed_seconds = 0;
            task.progress        = 0;
        }
        task.started_at = 0;
    }
    else if (status == Running)
    {
        if (previous_status == Stopped || previous_status == Failed)
        {
            task.elapsed_seconds = 0;
            task.progress        = 0;
        }
        if (task.started_at <= 0)
            task.started_at = now;
    }
    else if (status == Stopping)
    {
        if (task.status == Running && task.started_at <= 0)
            task.started_at = now;
    }
    else
    {
        task.started_at = 0;
    }

    task.status = status;
    if (status == Finished)
    {
        task.progress    = 100;
        task.eta_seconds = 0;
    }
    else if (status == Preparing || status == Stopping || status == Stopped || status == Failed)
    {
        task.eta_seconds = -1;
    }
    emitTaskChanged(row);
    return true;
}

void TaskManager::emitTaskChanged(const int row, const QList<int> &roles)
{
    if (row < 0 || row >= rowCount())
        return;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1), roles);
    ++revision_;
    emit revisionChanged();
}

void TaskManager::refreshRunningTasks()
{
    for (int row = 0; row < static_cast<int>(tasks_.size()); ++row)
    {
        const TaskStatus status = tasks_[static_cast<size_t>(row)].status;
        if (status == Running || status == Stopping)
            emit dataChanged(index(row, RunningTimeColumn), index(row, RunningTimeColumn),
                             {RunningTimeRole, Qt::DisplayRole});
    }
}

QVariant TaskManager::dataForColumn(const Task &task, const int column) const
{
    switch (column)
    {
    case TaskIdColumn:
        return task.id;
    case ModelNameColumn:
        return task.model_name;
    case TaskTypeColumn:
        return modelTaskDisplayName(task.type);
    case StatusColumn:
        return statusText(task);
    case CreatedAtColumn:
        return createdAtText(task);
    case RunningTimeColumn:
        return runningTimeText(task);
    case EtaColumn:
        return etaText(task);
    case ProgressColumn:
        return task.progress;
    case ActionsColumn:
        return task.id;
    default:
        return {};
    }
}

QString TaskManager::statusText(const Task &task) const
{
    switch (task.status)
    {
    case Pending:
        return QString("等待中");
    case Preparing:
        return QString("准备中");
    case Running:
        return QString("运行中");
    case Paused:
        return QString("已暂停");
    case Stopping:
        return QString("停止中");
    case Stopped:
        return QString("已停止");
    case Finished:
        return QString("已结束");
    case Failed:
        return QString("失败");
    default:
        return QString("未知");
    }
}

QString TaskManager::createdAtText(const Task &task) const
{
    return QDateTime::fromSecsSinceEpoch(task.created_at).toString(QStringLiteral("yyyy/MM/dd hh:mm:ss"));
}

QString TaskManager::runningTimeText(const Task &task) const
{
    return durationText(runningTimeSeconds(task));
}

QString TaskManager::etaText(const Task &task) const
{
    const qint64 seconds = etaSeconds(task);
    return seconds >= 0 ? durationText(seconds) : QStringLiteral("-");
}

qint64 TaskManager::runningTimeSeconds(const Task &task) const
{
    qint64 seconds = task.elapsed_seconds;
    if ((task.status == Running || task.status == Stopping) && task.started_at > 0)
        seconds += std::max<qint64>(0, QDateTime::currentSecsSinceEpoch() - task.started_at);
    return seconds;
}

qint64 TaskManager::etaSeconds(const Task &task) const
{
    return task.status == Finished || task.progress >= 100 ? 0 : task.eta_seconds;
}

bool TaskManager::canStart(const Task &task) const
{
    return task.status == Pending || task.status == Paused || task.status == Stopped || task.status == Failed;
}

bool TaskManager::canPause(const Task &task) const
{
    return task.supports_pause && task.status == Running;
}

bool TaskManager::canStop(const Task &task) const
{
    return task.status == Preparing || task.status == Running || task.status == Paused;
}

bool TaskManager::canDelete(const Task &task) const
{
    return task.status == Pending || isTerminal(task.status);
}

bool TaskManager::canFinish(const Task &task) const
{
    return task.status == Running || task.status == Paused;
}

} // namespace dltool::model
