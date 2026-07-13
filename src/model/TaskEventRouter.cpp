#include "model/TaskEventRouter.h"

#include "model/TaskCommunication.h"
#include "model/TaskManager.h"
#include "ui/SignalHelper.h"

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
        const QString error_prefix = QStringLiteral("[DLTOOL_ERROR] ");
        if (message.message.startsWith(error_prefix))
        {
            const QString detail = message.message.sliced(error_prefix.size());
            spdlog::error("任务 {} 失败详情: {}", message.task_id, detail.toUtf8().constData());
        }
        else if (!message.message.isEmpty())
        {
            spdlog::info("任务 {}: {}", message.task_id, message.message.toUtf8().constData());
        }
        return;
    }

    TaskTableModel *tasks = task_manager_->tasks();
    if (message.progress >= 0)
        tasks->updateTaskProgress(message.task_id, message.progress);
    if (message.payload.contains(taskProtocolFieldName(TaskProtocolField::EtaSeconds)) && message.eta_seconds >= 0)
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
        ui::SignalHelper::notifyError(
            QString("模型任务 %1 失败").arg(message.task_id),
            message.message.isEmpty() ? QString("任务执行失败，请查看模型日志。") : message.message);
        task_manager_->failTask(message.task_id);
        break;
    default:
        break;
    }
}

} // namespace dltool::model
