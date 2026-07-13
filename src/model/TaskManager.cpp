#include "model/TaskManager.h"

#include "model/TaskCommunication.h"
#include "model/TaskEventRouter.h"

#include <spdlog/spdlog.h>

#include <QDateTime>
#include <algorithm>

namespace dltool::model {

namespace {

constexpr int kRuntimeRefreshIntervalMs = 1000;

int boundedProgress(int progress)
{
    return std::clamp(progress, 0, 100);
}

QString durationText(qint64 seconds)
{
    seconds = std::max<qint64>(0, seconds);
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

TaskTableModel::TaskTableModel(QObject *parent)
    : QAbstractTableModel(parent)
    , runtime_timer_(new QTimer(this))
{
    runtime_timer_->setInterval(kRuntimeRefreshIntervalMs);
    connect(runtime_timer_, &QTimer::timeout, this, &TaskTableModel::refreshRunningTasks);
    runtime_timer_->start();
}

int TaskTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(tasks_.size());
}

int TaskTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

int TaskTableModel::count() const
{
    return rowCount();
}

int TaskTableModel::revision() const
{
    return revision_;
}

QVariant TaskTableModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount() || index.column() < 0 || index.column() >= columnCount())
        return QVariant();

    const TaskRecord &task = tasks_.at(static_cast<size_t>(index.row()));
    switch (role)
    {
    case Qt::DisplayRole:
        return dataForColumn(task, index.column());
    case TaskIdRole:
        return task.task_id;
    case ModelUuidRole:
        return task.model_uuid;
    case ModelNameRole:
        return task.model_name;
    case TaskTypeRole:
        return static_cast<int>(task.task_type);
    case TaskTypeTextRole:
        return modelTaskDisplayName(task.task_type);
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
    default:
        return QVariant();
    }
}

QVariant TaskTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

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
        return QStringLiteral("ETA");
    case ProgressColumn:
        return QString("进度");
    case ActionsColumn:
        return QString("操作");
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> TaskTableModel::roleNames() const
{
    return {
        {Qt::DisplayRole,      "display"},
        {     TaskIdRole,      "task_id"},
        {  ModelUuidRole,   "model_uuid"},
        {  ModelNameRole,   "model_name"},
        {   TaskTypeRole,    "task_type"},
        {TaskTypeTextRole, "task_type_text"},
        {     StatusRole,       "status"},
        {StatusValueRole, "status_value"},
        {  CreatedAtRole,   "created_at"},
        {RunningTimeRole, "running_time"},
        {       EtaRole,          "eta"},
        {   ProgressRole,     "progress"},
        {   CanStartRole,    "can_start"},
        {   CanPauseRole,    "can_pause"},
        {    CanStopRole,     "can_stop"},
        {  CanFinishRole,   "can_finish"},
    };
}

int TaskTableModel::addTask(const QString &model_uuid, const QString &model_name, ModelTaskType task_type,
                            bool supports_pause)
{
    const QString trimmed_model_uuid = model_uuid.trimmed();
    const QString trimmed_model_name = model_name.trimmed();
    if (trimmed_model_uuid.isEmpty() || trimmed_model_name.isEmpty() || !isKnownModelTask(task_type))
        return -1;

    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    const int task_id = next_task_id_++;
    tasks_.push_back(TaskRecord{
        task_id,
        trimmed_model_uuid,
        trimmed_model_name,
        task_type,
        Pending,
        QDateTime::currentSecsSinceEpoch(),
        0,
        0,
        -1,
        0,
        supports_pause,
    });
    endInsertRows();
    emit countChanged();
    bumpRevision();
    return task_id;
}

bool TaskTableModel::startTask(int task_id)
{
    const int row = indexOfTask(task_id);
    if (row < 0)
        return false;

    TaskRecord &task = tasks_[static_cast<size_t>(row)];
    if (!canStart(task))
        return false;

    task.status     = Running;
    task.started_at = QDateTime::currentSecsSinceEpoch();
    task.eta_seconds = -1;
    emitTaskChanged(row);
    return true;
}

bool TaskTableModel::pauseTask(int task_id)
{
    const int row = indexOfTask(task_id);
    if (row < 0)
        return false;

    TaskRecord &task = tasks_[static_cast<size_t>(row)];
    if (!canPause(task))
        return false;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (task.started_at > 0)
        task.accumulated_seconds += std::max<qint64>(0, now - task.started_at);
    task.started_at = 0;
    task.status     = Paused;
    emitTaskChanged(row);
    return true;
}

bool TaskTableModel::stopTask(int task_id)
{
    const int row = indexOfTask(task_id);
    if (row < 0)
        return false;

    TaskRecord &task = tasks_[static_cast<size_t>(row)];
    if (!canStop(task))
        return false;

    if (task.status == Running && task.started_at <= 0)
        task.started_at = QDateTime::currentSecsSinceEpoch();
    task.status      = Stopping;
    task.eta_seconds = -1;
    emitTaskChanged(row);
    return true;
}

bool TaskTableModel::finishTask(int task_id)
{
    const int row = indexOfTask(task_id);
    if (row < 0)
        return false;

    TaskRecord &task = tasks_[static_cast<size_t>(row)];
    if (!canFinish(task))
        return false;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if ((task.status == Running || task.status == Stopping) && task.started_at > 0)
        task.accumulated_seconds += std::max<qint64>(0, now - task.started_at);
    task.started_at = 0;
    task.status     = Finished;
    task.progress   = 100;
    task.eta_seconds = 0;
    emitTaskChanged(row);
    return true;
}

bool TaskTableModel::failTask(int task_id)
{
    return setTaskStatus(task_id, Failed);
}

bool TaskTableModel::setTaskStatus(int task_id, TaskStatus status)
{
    const int row = indexOfTask(task_id);
    if (row < 0)
        return false;

    TaskRecord &task = tasks_[static_cast<size_t>(row)];
    if (task.status == status)
        return true;

    const qint64 now          = QDateTime::currentSecsSinceEpoch();
    const bool   was_counting = (task.status == Running || task.status == Stopping) && task.started_at > 0;
    const bool   will_count   = status == Running || status == Stopping;
    if (was_counting && !will_count)
        task.accumulated_seconds += std::max<qint64>(0, now - task.started_at);
    if (status == Running)
    {
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
    task.status     = status;
    if (status == Finished)
    {
        task.progress = 100;
        task.eta_seconds = 0;
    }
    else if (status == Stopping || status == Stopped || status == Failed)
    {
        task.eta_seconds = -1;
    }
    emitTaskChanged(row);
    return true;
}

bool TaskTableModel::deleteTask(int task_id)
{
    const int row = indexOfTask(task_id);
    if (row < 0)
        return false;

    beginRemoveRows(QModelIndex(), row, row);
    tasks_.erase(tasks_.begin() + row);
    endRemoveRows();
    emit countChanged();
    bumpRevision();
    return true;
}

bool TaskTableModel::updateTaskProgress(int task_id, int progress)
{
    const int row = indexOfTask(task_id);
    if (row < 0)
        return false;

    TaskRecord &task    = tasks_[static_cast<size_t>(row)];
    const int   bounded = boundedProgress(progress);
    bool eta_changed = false;
    if (bounded >= 100 && task.eta_seconds != 0)
    {
        task.eta_seconds = 0;
        eta_changed = true;
    }

    if (task.progress == bounded && !eta_changed)
        return true;

    task.progress = bounded;
    if (eta_changed)
        emit dataChanged(index(row, EtaColumn), index(row, ProgressColumn),
                         {EtaRole, ProgressRole, Qt::DisplayRole});
    else
        emit dataChanged(index(row, ProgressColumn), index(row, ProgressColumn), {ProgressRole, Qt::DisplayRole});
    return true;
}

bool TaskTableModel::updateTaskEta(int task_id, qint64 eta_seconds)
{
    const int row = indexOfTask(task_id);
    if (row < 0)
        return false;

    TaskRecord &task    = tasks_[static_cast<size_t>(row)];
    const qint64 bounded = eta_seconds < 0 ? -1 : eta_seconds;
    if (task.eta_seconds == bounded)
        return true;

    task.eta_seconds = bounded;
    emit dataChanged(index(row, EtaColumn), index(row, EtaColumn), {EtaRole, Qt::DisplayRole});
    return true;
}

void TaskTableModel::clearTasks()
{
    if (tasks_.empty())
        return;

    beginResetModel();
    tasks_.clear();
    next_task_id_ = 1;
    endResetModel();
    emit countChanged();
    bumpRevision();
}

int TaskTableModel::findModelTask(const QString &model_uuid, ModelTaskType task_type, bool include_finished) const
{
    const int row = indexOfModelTask(model_uuid.trimmed(), task_type, include_finished);
    if (row < 0)
        return -1;
    return tasks_.at(static_cast<size_t>(row)).task_id;
}

TaskTableModel::TaskSnapshot TaskTableModel::taskSnapshotForId(int task_id) const
{
    const int row = indexOfTask(task_id);
    if (row < 0)
        return {};
    return snapshotForRow(row);
}

TaskTableModel::TaskSnapshot TaskTableModel::taskSnapshotForModel(const QString &model_uuid, ModelTaskType task_type,
                                                                  bool include_finished) const
{
    const int row = indexOfModelTask(model_uuid.trimmed(), task_type, include_finished);
    if (row < 0)
        return {};
    return snapshotForRow(row);
}

QVariantMap TaskTableModel::taskForId(int task_id) const
{
    const int row = indexOfTask(task_id);
    if (row < 0)
        return {};
    return taskAt(row);
}

QVariantMap TaskTableModel::taskForModel(const QString &model_uuid, ModelTaskType task_type,
                                         bool include_finished) const
{
    const int row = indexOfModelTask(model_uuid.trimmed(), task_type, include_finished);
    if (row < 0)
        return {};
    return taskAt(row);
}

QVariantMap TaskTableModel::taskAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};

    const TaskRecord &task = tasks_.at(static_cast<size_t>(row));
    return {
        {     QStringLiteral("task_id"),          task.task_id},
        {  QStringLiteral("model_uuid"),       task.model_uuid},
        {  QStringLiteral("model_name"),       task.model_name},
        {   QStringLiteral("task_type"), static_cast<int>(task.task_type)},
        {QStringLiteral("task_type_text"), modelTaskDisplayName(task.task_type)},
        {      QStringLiteral("status"),      statusText(task)},
        {QStringLiteral("status_value"),           task.status},
        {  QStringLiteral("created_at"),   createdAtText(task)},
        {QStringLiteral("running_time"), runningTimeText(task)},
        {         QStringLiteral("eta"),       etaText(task)},
        {    QStringLiteral("progress"),         task.progress},
        {   QStringLiteral("can_start"),        canStart(task)},
        {   QStringLiteral("can_pause"),        canPause(task)},
        {    QStringLiteral("can_stop"),         canStop(task)},
        {  QStringLiteral("can_finish"),       canFinish(task)},
    };
}

int TaskTableModel::indexOfTask(int task_id) const
{
    for (int row = 0; row < static_cast<int>(tasks_.size()); ++row)
    {
        if (tasks_[static_cast<size_t>(row)].task_id == task_id)
            return row;
    }
    return -1;
}

int TaskTableModel::indexOfModelTask(const QString &model_uuid, ModelTaskType task_type, bool include_finished) const
{
    if (model_uuid.isEmpty() || !isKnownModelTask(task_type))
        return -1;

    for (int row = static_cast<int>(tasks_.size()) - 1; row >= 0; --row)
    {
        const TaskRecord &task = tasks_.at(static_cast<size_t>(row));
        if (task.model_uuid != model_uuid)
            continue;
        if (task.task_type != task_type)
            continue;
        if (!include_finished && task.status == Finished)
            continue;
        return row;
    }
    return -1;
}

TaskTableModel::TaskSnapshot TaskTableModel::snapshotForRow(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};

    const TaskRecord &task = tasks_.at(static_cast<size_t>(row));
    return TaskSnapshot{
        task.task_id,
        task.model_uuid,
        task.model_name,
        task.task_type,
        task.status,
        task.created_at,
        task.started_at,
        task.accumulated_seconds,
        etaSeconds(task),
        task.progress,
        task.supports_pause,
        canStart(task),
        canPause(task),
        canStop(task),
        canFinish(task),
    };
}

void TaskTableModel::emitTaskChanged(int row, const QList<int> &roles)
{
    if (row < 0 || row >= rowCount())
        return;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1), roles);
    bumpRevision();
}

void TaskTableModel::bumpRevision()
{
    ++revision_;
    emit revisionChanged();
}

void TaskTableModel::refreshRunningTasks()
{
    for (int row = 0; row < static_cast<int>(tasks_.size()); ++row)
    {
        const TaskStatus status = tasks_[static_cast<size_t>(row)].status;
        if (status == Running || status == Stopping)
        {
            emit dataChanged(index(row, RunningTimeColumn), index(row, RunningTimeColumn),
                             {RunningTimeRole, Qt::DisplayRole});
        }
    }
}

QVariant TaskTableModel::dataForColumn(const TaskRecord &task, int column) const
{
    switch (column)
    {
    case TaskIdColumn:
        return task.task_id;
    case ModelNameColumn:
        return task.model_name;
    case TaskTypeColumn:
        return modelTaskDisplayName(task.task_type);
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
        return task.task_id;
    default:
        return QVariant();
    }
}

QString TaskTableModel::statusText(const TaskRecord &task) const
{
    switch (task.status)
    {
    case Pending:
        return QString("等待中");
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

QString TaskTableModel::createdAtText(const TaskRecord &task) const
{
    return QDateTime::fromSecsSinceEpoch(task.created_at).toString(QStringLiteral("yyyy/MM/dd hh:mm:ss"));
}

QString TaskTableModel::runningTimeText(const TaskRecord &task) const
{
    return durationText(runningTimeSeconds(task));
}

QString TaskTableModel::etaText(const TaskRecord &task) const
{
    const qint64 seconds = etaSeconds(task);
    return seconds >= 0 ? durationText(seconds) : QStringLiteral("-");
}

qint64 TaskTableModel::runningTimeSeconds(const TaskRecord &task) const
{
    qint64 seconds = task.accumulated_seconds;
    if ((task.status == Running || task.status == Stopping) && task.started_at > 0)
        seconds += std::max<qint64>(0, QDateTime::currentSecsSinceEpoch() - task.started_at);
    return seconds;
}

qint64 TaskTableModel::etaSeconds(const TaskRecord &task) const
{
    if (task.status == Finished || task.progress >= 100)
        return 0;
    return task.eta_seconds;
}

bool TaskTableModel::canStart(const TaskRecord &task) const
{
    return task.status == Pending || task.status == Paused || task.status == Stopped || task.status == Failed;
}

bool TaskTableModel::canPause(const TaskRecord &task) const
{
    if (!task.supports_pause)
        return false;
    return task.status == Running;
}

bool TaskTableModel::canStop(const TaskRecord &task) const
{
    return task.status == Running || task.status == Paused;
}

bool TaskTableModel::canFinish(const TaskRecord &task) const
{
    return task.status == Running || task.status == Paused || task.status == Stopping || task.status == Stopped;
}

bool TaskTableModel::isTerminal(const TaskRecord &task) const
{
    return task.status == Stopped || task.status == Finished || task.status == Failed;
}

TaskManager::TaskManager(QObject *parent)
    : QObject(parent)
    , tasks_(new TaskTableModel(this))
    , communication_server_(new TaskCommunicationServer(this))
    , event_router_(new TaskEventRouter(this, this))
{
    connect(communication_server_, &TaskCommunicationServer::messageReceived, event_router_,
            &TaskEventRouter::handleTaskMessage);
}

int TaskManager::addTask(const QString &model_uuid, const QString &model_name, ModelTaskType task_type)
{
    const int task_id = tasks_->addTask(model_uuid, model_name, task_type);
    spdlog::info("通用任务添加{}, task_id: {}, 模型: {}, 类型: {}", task_id >= 0 ? "成功" : "失败", task_id,
                 model_name.toUtf8().constData(), modelTaskKey(task_type).toUtf8().constData());
    return task_id;
}

int TaskManager::addTask(const QString &model_uuid, const QString &model_name, ModelTaskType task_type,
                         bool supports_pause)
{
    const int task_id = tasks_->addTask(model_uuid, model_name, task_type, supports_pause);
    spdlog::info("通用任务添加{}, task_id: {}, 模型: {}, 类型: {}", task_id >= 0 ? "成功" : "失败", task_id,
                 model_name.toUtf8().constData(), modelTaskKey(task_type).toUtf8().constData());
    return task_id;
}

bool TaskManager::startTask(int task_id)
{
    const bool started = tasks_->startTask(task_id);
    spdlog::info("通用任务启动{}, task_id: {}", started ? "成功" : "失败", task_id);
    return started;
}

bool TaskManager::pauseTask(int task_id)
{
    const bool paused = tasks_->pauseTask(task_id);
    spdlog::info("通用任务暂停{}, task_id: {}", paused ? "成功" : "失败", task_id);
    return paused;
}

bool TaskManager::stopTask(int task_id)
{
    const TaskTableModel::TaskSnapshot task = tasks_->taskSnapshotForId(task_id);
    spdlog::info("通用任务停止请求, task_id: {}, 模型: {}, 类型: {}", task_id,
                 task.model_name.toUtf8().constData(), modelTaskKey(task.task_type).toUtf8().constData());
    const bool changed = tasks_->stopTask(task_id);
    if (!changed)
    {
        spdlog::warn("通用任务停止失败, task_id: {}", task_id);
        return false;
    }
    const bool command_sent = requestStopTask(task_id);
    emit taskStopRequested(task_id);
    spdlog::info("通用任务停止命令已发送, task_id: {}, 通信命令: {}", task_id, command_sent ? "成功" : "未发送");
    return true;
}

bool TaskManager::finishTask(int task_id)
{
    return tasks_->finishTask(task_id);
}

bool TaskManager::failTask(int task_id)
{
    return tasks_->failTask(task_id);
}

bool TaskManager::markTaskStopped(int task_id)
{
    return tasks_->setTaskStatus(task_id, TaskTableModel::Stopped);
}

bool TaskManager::deleteTask(int task_id)
{
    const TaskTableModel::TaskSnapshot task = tasks_->taskSnapshotForId(task_id);
    spdlog::info("通用任务删除请求, task_id: {}, 模型: {}, 类型: {}", task_id,
                 task.model_name.toUtf8().constData(), modelTaskKey(task.task_type).toUtf8().constData());
    const bool command_sent = requestStopTask(task_id);
    if (task.can_stop || task.status == TaskTableModel::Stopping)
        emit taskStopRequested(task_id);
    const bool deleted = tasks_->deleteTask(task_id);
    spdlog::info("通用任务删除{}, task_id: {}, 停止命令: {}", deleted ? "成功" : "失败", task_id,
                 command_sent ? "已发送" : "未发送");
    return deleted;
}

bool TaskManager::updateTaskProgress(int task_id, int progress)
{
    return tasks_->updateTaskProgress(task_id, progress);
}

bool TaskManager::updateTaskEta(int task_id, qint64 eta_seconds)
{
    return tasks_->updateTaskEta(task_id, eta_seconds);
}

void TaskManager::clearTasks()
{
    tasks_->clearTasks();
}

bool TaskManager::ensureTaskServer(QString *err_msg)
{
    return communication_server_ != nullptr && communication_server_->start(err_msg);
}

QString TaskManager::taskServerHost() const
{
    return communication_server_ ? communication_server_->host() : QStringLiteral("127.0.0.1");
}

quint16 TaskManager::taskServerPort() const
{
    return communication_server_ ? communication_server_->port() : 0;
}

bool TaskManager::requestStopTask(int task_id)
{
    if (communication_server_ == nullptr)
        return false;
    return communication_server_->sendCommand(task_id, TaskCommand::Stop);
}

} // namespace dltool::model
