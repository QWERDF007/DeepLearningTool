#include "ui/UILogger.h"

#include "ui/Color.h"

#include <QTextCursor>
#include <QTextDocument>

namespace dltool::ui {

void UILogger::log(const int level, const QString &message)
{
    if (level == spdlog::level::err)
        ++error_count_;
    else
        ++info_count_;

    queue_.enqueue(std::make_pair(level, message + "\n"));

    while (queue_.size() > max_size_)
    {
        queue_.dequeue();
    }

    emit countChanged();
    emit messageChanged();
}

void UILogger::clearCount()
{
    info_count_  = 0;
    error_count_ = 0;
    emit countChanged();
}

QString UILogger::getMessage() const
{
    QString result;
    size_t  size = 0;
    for (const auto &[level, msg] : queue_)
    {
        size += msg.size();
    }
    result.reserve(size);
    for (const auto &[level, msg] : queue_)
    {
        result += msg;
    }
    return result;
}

QString UILogger::getColorfulMessage() const
{
    QTextDocument document;
    QTextCursor   cursor(&document);
    cursor.beginEditBlock();
    for (const auto &[level, msg] : queue_)
    {
        if (level == spdlog::level::err)
        {
            QTextCharFormat format;
            format.setForeground(QColor("red"));
            cursor.insertText(msg, format);
            format.setForeground(DltColor::getInstance()->FontPrimary());
            cursor.setCharFormat(format);
        }
        else
        {
            cursor.insertText(msg);
        }
    }
    cursor.endEditBlock();
    return document.toHtml();
}

} // namespace dltool::ui
