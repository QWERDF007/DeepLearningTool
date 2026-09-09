#include "ui/ProgressManager.h"

#include <spdlog/spdlog.h>

#include <QTextCursor>
#include <QTextDocument>

namespace dltool::ui {

ProgressManager::ProgressManager(QObject *parent)
    : QObject(parent)
{
}

QString ProgressManager::getMessage() const
{
    QString result;
    size_t  size = 0;
    for (const auto &[level, msg] : message_queue_)
    {
        size += msg.size();
    }
    result.reserve(size);
    for (const auto &[level, msg] : message_queue_)
    {
        result += msg;
        if (!msg.endsWith('\n'))
        {
            result += '\n';
        }
    }
    return result;
}

QString ProgressManager::getColorfulMessage() const
{
    // 返回普通文本而不是 HTML
    return getMessage();
}

QString ProgressManager::generateUniqueTaskId()
{
    return QStringLiteral("task_%1").arg(++task_seq_);
}

QString ProgressManager::startTask(const QString &taskName, const QString &taskId)
{
    const QString previous_task_id = active_task_id_;
    const QString previous_task_name = task_name_;
    const bool was_running   = is_running_;
    const bool had_progress  = (progress_ != 0);
    const bool had_messages  = !message_queue_.isEmpty();

    if (taskId.isEmpty())
    {
        active_task_id_ = generateUniqueTaskId();
        is_anonymous_   = true;
    }
    else
    {
        active_task_id_ = taskId;
        is_anonymous_   = false;
    }

    task_name_  = taskName;
    progress_   = 0;
    is_running_ = true;
    message_queue_.clear();

    if (previous_task_id != active_task_id_)
        emit activeTaskIdChanged();
    if (previous_task_name != task_name_)
        emit taskNameChanged();
    if (had_progress || !was_running)
        emit progressChanged();
    if (!was_running)
        emit runningStateChanged();
    if (had_messages)
        emit messageChanged();

    return active_task_id_;
}

void ProgressManager::updateProgress(int progress, const QString &taskId)
{
    if (!is_running_)
    {
        if (taskId.isEmpty())
        {
            startTask(QStringLiteral("后台任务"), QString());
        }
        else
        {
            return;
        }
    }

    if (!taskId.isEmpty())
    {
        if (taskId != active_task_id_)
        {
            return;
        }
    }
    else
    {
        if (!is_anonymous_)
        {
            return;
        }
    }

    // 验证并将进度值限制在 [0, 100] 范围内
    if (progress < 0)
    {
        spdlog::warn("进度值 {} 小于 0，限制为 0", progress);
        progress = 0;
    }
    else if (progress > 100)
    {
        spdlog::warn("进度值 {} 超过 100，限制为 100", progress);
        progress = 100;
    }

    if (progress_ != progress)
    {
        progress_ = progress;
        emit progressChanged();
    }
}

void ProgressManager::addMessage(int level, const QString &message, const QString &taskId)
{
    if (!taskId.isEmpty())
    {
        if (taskId != active_task_id_)
        {
            return;
        }
    }
    else
    {
        if (!is_anonymous_ && is_running_)
        {
            return;
        }
    }

    message_queue_.enqueue(std::make_pair(level, message));

    // 处理队列溢出（FIFO）
    while (message_queue_.size() > max_message_size_)
    {
        message_queue_.dequeue();
    }

    emit messageChanged();
}

void ProgressManager::completeTask(const QString &taskId, bool success)
{
    finishTask(taskId, success);
}

void ProgressManager::finishTask(const QString &taskId, bool success)
{
    if (!taskId.isEmpty())
    {
        if (taskId != active_task_id_)
        {
            return;
        }
    }
    else
    {
        if (!is_anonymous_ && is_running_)
        {
            return;
        }
    }

    if (!is_running_)
    {
        return;
    }

    is_running_ = false;

    if (success)
    {
        if (progress_ != 100)
        {
            progress_ = 100;
            emit progressChanged();
        }
    }

    emit runningStateChanged();
}

void ProgressManager::reset()
{
    const bool was_running  = is_running_;
    const bool had_progress = (progress_ != 0);
    const bool had_messages = !message_queue_.isEmpty();
    const bool had_task     = !active_task_id_.isEmpty() || !task_name_.isEmpty();

    progress_     = 0;
    is_running_   = false;
    is_anonymous_ = false;
    active_task_id_.clear();
    task_name_.clear();
    message_queue_.clear();

    if (had_progress)
        emit progressChanged();
    if (was_running)
        emit runningStateChanged();
    if (had_messages)
        emit messageChanged();
    if (had_task)
    {
        emit activeTaskIdChanged();
        emit taskNameChanged();
    }
}

} // namespace dltool::ui
