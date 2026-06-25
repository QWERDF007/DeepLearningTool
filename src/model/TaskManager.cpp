#include "model/TaskManager.h"

#include "model/TaskCommunication.h"

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
        return task.task_type;
    case StatusRole:
        return statusText(task);
    case StatusValueRole:
        return task.status;
    case CreatedAtRole:
        return createdAtText(task);
    case RunningTimeRole:
        return runningTimeText(task);
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
        return QStringLiteral("任务ID");
    case ModelNameColumn:
        return QStringLiteral("模型名称");
    case TaskTypeColumn:
        return QStringLiteral("任务类型");
    case StatusColumn:
        return QStringLiteral("任务状态");
    case CreatedAtColumn:
        return QStringLiteral("任务创建时间");
    case RunningTimeColumn:
        return QStringLiteral("运行时间");
    case ProgressColumn:
        return QStringLiteral("进度");
    case ActionsColumn:
        return QStringLiteral("操作");
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
        {     StatusRole,       "status"},
        {StatusValueRole, "status_value"},
        {  CreatedAtRole,   "created_at"},
        {RunningTimeRole, "running_time"},
        {   ProgressRole,     "progress"},
        {   CanStartRole,    "can_start"},
        {   CanPauseRole,    "can_pause"},
        {    CanStopRole,     "can_stop"},
        {  CanFinishRole,   "can_finish"},
    };
}

int TaskTableModel::addTask(const QString &model_uuid, const QString &model_name, const QString &task_type,
                            bool external_process, bool supports_pause)
{
    const QString trimmed_model_uuid = model_uuid.trimmed();
    const QString trimmed_model_name = model_name.trimmed();
    if (trimmed_model_uuid.isEmpty() || trimmed_model_name.isEmpty())
        return -1;

    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    const int task_id = next_task_id_++;
    tasks_.push_back(TaskRecord{
        task_id,
        trimmed_model_uuid,
        trimmed_model_name,
        task_type.trimmed(),
        Pending,
        QDateTime::currentSecsSinceEpoch(),
        0,
        0,
        0,
        external_process,
        supports_pause,
    });
    endInsertRows();
    emit countChanged();
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

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (task.status == Running && task.started_at > 0)
        task.accumulated_seconds += std::max<qint64>(0, now - task.started_at);
    task.started_at = 0;
    task.status     = Stopped;
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
    if (task.status == Running && task.started_at > 0)
        task.accumulated_seconds += std::max<qint64>(0, now - task.started_at);
    task.started_at = 0;
    task.status     = Finished;
    task.progress   = 100;
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

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (task.status == Running && task.started_at > 0)
        task.accumulated_seconds += std::max<qint64>(0, now - task.started_at);
    task.started_at = status == Running ? now : 0;
    task.status     = status;
    if (status == Finished)
        task.progress = 100;
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
    return true;
}

bool TaskTableModel::updateTaskProgress(int task_id, int progress)
{
    const int row = indexOfTask(task_id);
    if (row < 0)
        return false;

    TaskRecord &task    = tasks_[static_cast<size_t>(row)];
    const int   bounded = boundedProgress(progress);
    if (task.progress == bounded)
        return true;

    task.progress = bounded;
    emit dataChanged(index(row, ProgressColumn), index(row, ProgressColumn), {ProgressRole, Qt::DisplayRole});
    return true;
}

int TaskTableModel::startModelTask(const QString &model_uuid, const QString &model_name, const QString &task_type)
{
    const QString trimmed_model_uuid = model_uuid.trimmed();
    if (trimmed_model_uuid.isEmpty())
        return -1;

    int row = indexOfModelTask(trimmed_model_uuid, task_type.trimmed(), false);
    int task_id{-1};
    if (row < 0)
    {
        task_id = addTask(trimmed_model_uuid, model_name, task_type);
        row     = indexOfTask(task_id);
    }
    else
    {
        task_id = tasks_.at(static_cast<size_t>(row)).task_id;
    }

    if (row < 0)
        return -1;

    TaskRecord &task = tasks_[static_cast<size_t>(row)];
    if (task.status == Running)
        return task.task_id;

    startTask(task.task_id);
    return task.task_id;
}

bool TaskTableModel::stopModelTask(const QString &model_uuid, const QString &task_type)
{
    const int row = indexOfModelTask(model_uuid.trimmed(), task_type.trimmed(), false);
    if (row < 0)
        return false;

    return stopTask(tasks_.at(static_cast<size_t>(row)).task_id);
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
        {   QStringLiteral("task_type"),        task.task_type},
        {      QStringLiteral("status"),      statusText(task)},
        {QStringLiteral("status_value"),           task.status},
        {  QStringLiteral("created_at"),   createdAtText(task)},
        {QStringLiteral("running_time"), runningTimeText(task)},
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

int TaskTableModel::indexOfModelTask(const QString &model_uuid, const QString &task_type, bool include_finished) const
{
    if (model_uuid.isEmpty())
        return -1;

    for (int row = static_cast<int>(tasks_.size()) - 1; row >= 0; --row)
    {
        const TaskRecord &task = tasks_.at(static_cast<size_t>(row));
        if (task.model_uuid != model_uuid)
            continue;
        if (!task_type.isEmpty() && task.task_type != task_type)
            continue;
        if (!include_finished && task.status == Finished)
            continue;
        return row;
    }
    return -1;
}

void TaskTableModel::emitTaskChanged(int row, const QList<int> &roles)
{
    if (row < 0 || row >= rowCount())
        return;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1), roles);
}

void TaskTableModel::refreshRunningTasks()
{
    for (int row = 0; row < static_cast<int>(tasks_.size()); ++row)
    {
        if (tasks_[static_cast<size_t>(row)].status == Running)
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
        return task.task_type;
    case StatusColumn:
        return statusText(task);
    case CreatedAtColumn:
        return createdAtText(task);
    case RunningTimeColumn:
        return runningTimeText(task);
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
        return QStringLiteral("等待中");
    case Running:
        return QStringLiteral("运行中");
    case Paused:
        return QStringLiteral("已暂停");
    case Stopped:
        return QStringLiteral("已停止");
    case Finished:
        return QStringLiteral("已结束");
    case Failed:
        return QStringLiteral("失败");
    default:
        return QStringLiteral("未知");
    }
}

QString TaskTableModel::createdAtText(const TaskRecord &task) const
{
    return QDateTime::fromSecsSinceEpoch(task.created_at).toString(QStringLiteral("yyyy/MM/dd hh:mm:ss"));
}

QString TaskTableModel::runningTimeText(const TaskRecord &task) const
{
    qint64       seconds = runningTimeSeconds(task);
    const qint64 hours   = seconds / 3600;
    seconds %= 3600;
    const qint64 minutes = seconds / 60;
    seconds %= 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

qint64 TaskTableModel::runningTimeSeconds(const TaskRecord &task) const
{
    qint64 seconds = task.accumulated_seconds;
    if (task.status == Running && task.started_at > 0)
        seconds += std::max<qint64>(0, QDateTime::currentSecsSinceEpoch() - task.started_at);
    return seconds;
}

bool TaskTableModel::canStart(const TaskRecord &task) const
{
    if (task.external_process)
        return false;
    return task.status == Pending || task.status == Paused || task.status == Stopped;
}

bool TaskTableModel::canPause(const TaskRecord &task) const
{
    if (!task.supports_pause)
        return false;
    return task.status == Running;
}

bool TaskTableModel::canStop(const TaskRecord &task) const
{
    if (task.external_process)
        return !isTerminal(task);
    return task.status == Running || task.status == Paused;
}

bool TaskTableModel::canFinish(const TaskRecord &task) const
{
    if (task.external_process)
        return false;
    return task.status == Running || task.status == Paused || task.status == Stopped;
}

bool TaskTableModel::isTerminal(const TaskRecord &task) const
{
    return task.status == Stopped || task.status == Finished || task.status == Failed;
}

TaskManager::TaskManager(QObject *parent)
    : QObject(parent)
    , tasks_(new TaskTableModel(this))
    , communication_server_(new TaskCommunicationServer(this))
{
    connect(communication_server_, &TaskCommunicationServer::messageReceived, this, &TaskManager::handleTaskMessage);
}

int TaskManager::addTask(const QString &model_uuid, const QString &model_name, const QString &task_type)
{
    return tasks_->addTask(model_uuid, model_name, task_type);
}

int TaskManager::addExternalTask(const QString &model_uuid, const QString &model_name, const QString &task_type)
{
    return tasks_->addTask(model_uuid, model_name, task_type, true, false);
}

bool TaskManager::startTask(int task_id)
{
    return tasks_->startTask(task_id);
}

bool TaskManager::pauseTask(int task_id)
{
    return tasks_->pauseTask(task_id);
}

bool TaskManager::stopTask(int task_id)
{
    const bool changed = tasks_->stopTask(task_id);
    if (communication_server_ != nullptr)
        communication_server_->sendCommand(task_id, TaskCommand::Stop);
    emit taskStopRequested(task_id);
    return changed;
}

bool TaskManager::finishTask(int task_id)
{
    return tasks_->finishTask(task_id);
}

bool TaskManager::failTask(int task_id)
{
    return tasks_->failTask(task_id);
}

bool TaskManager::deleteTask(int task_id)
{
    return tasks_->deleteTask(task_id);
}

bool TaskManager::updateTaskProgress(int task_id, int progress)
{
    return tasks_->updateTaskProgress(task_id, progress);
}

int TaskManager::startModelTask(const QString &model_uuid, const QString &model_name, const QString &task_type)
{
    return tasks_->startModelTask(model_uuid, model_name, task_type);
}

bool TaskManager::stopModelTask(const QString &model_uuid, const QString &task_type)
{
    return tasks_->stopModelTask(model_uuid, task_type);
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

void TaskManager::handleTaskMessage(const TaskMessage &message)
{
    if (message.task_id < 0)
        return;

    if (message.progress >= 0)
        tasks_->updateTaskProgress(message.task_id, message.progress);

    switch (message.status)
    {
    case TaskProtocolStatus::Running:
        tasks_->setTaskStatus(message.task_id, TaskTableModel::Running);
        break;
    case TaskProtocolStatus::Paused:
        tasks_->setTaskStatus(message.task_id, TaskTableModel::Paused);
        break;
    case TaskProtocolStatus::Stopped:
        tasks_->setTaskStatus(message.task_id, TaskTableModel::Stopped);
        break;
    case TaskProtocolStatus::Finished:
        tasks_->setTaskStatus(message.task_id, TaskTableModel::Finished);
        break;
    case TaskProtocolStatus::Failed:
    case TaskProtocolStatus::Error:
        spdlog::error("任务 {} 失败: {}", message.task_id, message.message.toUtf8().constData());
        tasks_->failTask(message.task_id);
        break;
    default:
        break;
    }
}

} // namespace dltool::model
