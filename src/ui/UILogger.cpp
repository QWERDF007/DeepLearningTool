#include "ui/UILogger.h"

#include <QStringBuilder>
#include <chrono>

namespace dltool::ui {

void UILogger::log(const QString &message)
{
    queue_.enqueue(message + "\n");

    // 保证队列长度不超过 100 条
    while (queue_.size() > max_size_)
    {
        queue_.dequeue();
    }
    emit messageChanged();
}

QString UILogger::getMessage() const
{
    auto    start = std::chrono::high_resolution_clock::now();
    QString result;
    for (const auto &part : queue_)
    {
        // result = result % part; // 使用 QStringBuilder (% 拼接)
        result += part;
    }
    auto   end      = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end - start).count();
    qInfo() << __FUNCTION__ << __LINE__ << "concat time: " << duration << "ms";
    return result;
}

} // namespace dltool::ui
