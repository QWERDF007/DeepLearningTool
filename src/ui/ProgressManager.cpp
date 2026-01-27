#include "ui/ProgressManager.h"

#include "ui/Color.h"

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

void ProgressManager::startTask(const QString &taskName)
{
    task_name_  = taskName;
    progress_   = 0;
    is_running_ = true;

    emit progressChanged();
    emit runningStateChanged();
}

void ProgressManager::updateProgress(int progress)
{
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

void ProgressManager::addMessage(int level, const QString &message)
{
    message_queue_.enqueue(std::make_pair(level, message));

    // 处理队列溢出（FIFO）
    while (message_queue_.size() > max_message_size_)
    {
        message_queue_.dequeue();
    }

    emit messageChanged();
}

void ProgressManager::completeTask()
{
    if (!is_running_)
    {
        spdlog::warn("调用了 completeTask() 但没有任务正在运行");
    }

    progress_   = 100;
    is_running_ = false;

    emit progressChanged();
    emit runningStateChanged();
}

void ProgressManager::reset()
{
    progress_   = 0;
    is_running_ = false;
    task_name_.clear();
    message_queue_.clear();

    emit progressChanged();
    emit runningStateChanged();
    emit messageChanged();
}

} // namespace dltool::ui
