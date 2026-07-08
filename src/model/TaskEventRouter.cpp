#include "model/TaskEventRouter.h"

#include "model/TaskCommunication.h"
#include "model/TaskManager.h"

#include <spdlog/spdlog.h>

namespace dltool::model {

TaskEventRouter::TaskEventRouter(TaskManager *task_manager, QObject *parent)
    : QObject(parent)
    , task_manager_(task_manager)
{
}

void TaskEventRouter::handleTaskMessage(const TaskMessage &message)
{
    if (task_manager_ == nullptr || task_manager_->tasks() == nullptr || message.task_id < 0)
        return;

    if (message.type == TaskMessageType::Log)
    {
        if (!message.message.isEmpty())
            spdlog::info("任务 {}: {}", message.task_id, message.message.toUtf8().constData());
        return;
    }

    TaskTableModel *tasks = task_manager_->tasks();
    if (message.progress >= 0)
        tasks->updateTaskProgress(message.task_id, message.progress);
    if (message.payload.contains(taskProtocolFieldName(TaskProtocolField::EtaSeconds)))
        tasks->updateTaskEta(message.task_id, message.eta_seconds);

    switch (message.status)
    {
    case TaskProtocolStatus::Running:
        tasks->setTaskStatus(message.task_id, TaskTableModel::Running);
        break;
    case TaskProtocolStatus::Paused:
        tasks->setTaskStatus(message.task_id, TaskTableModel::Paused);
        break;
    case TaskProtocolStatus::Stopped:
        task_manager_->markTaskStopped(message.task_id);
        break;
    case TaskProtocolStatus::Finished:
        tasks->setTaskStatus(message.task_id, TaskTableModel::Finished);
        break;
    case TaskProtocolStatus::Failed:
    case TaskProtocolStatus::Error:
        spdlog::error("任务 {} 失败: {}", message.task_id, message.message.toUtf8().constData());
        task_manager_->failTask(message.task_id);
        break;
    default:
        break;
    }
}

} // namespace dltool::model
