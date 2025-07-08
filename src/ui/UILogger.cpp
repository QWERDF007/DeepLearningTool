#include "ui/UILogger.h"

#include "ui/Color.h"

#include <QTextCursor>
#include <QTextDocument>

namespace dltool::ui {

void UILogger::log(const QString &message)
{
    queue_.enqueue(message + "\n");

    // 保证队列长度不超过 max_size_ 条
    while (queue_.size() > max_size_)
    {
        queue_.dequeue();
    }
    emit messageChanged();
}

QString UILogger::getMessage() const
{
    QString result;
    size_t  size = 0;
    for (const auto &part : queue_)
    {
        size += part.size();
    }
    result.reserve(size);
    for (const auto &part : queue_)
    {
        result += part;
    }
    return result;
}

QString UILogger::getColorfulMessage() const
{
    QTextDocument document;
    QTextCursor   cursor(&document);
    cursor.beginEditBlock();
    for (int i = 0; i < queue_.size(); ++i)
    {
        if (i % 2 == 0)
        {
            QTextCharFormat format;
            format.setForeground(QColor("red"));
            cursor.insertText(queue_[i], format);
            format.setForeground(DltColor::getInstance()->FontPrimary());
            cursor.setCharFormat(format);
        }
        else
        {
            cursor.insertText(queue_[i]);
        }
    }
    cursor.endEditBlock();
    return document.toHtml();
}

} // namespace dltool::ui
