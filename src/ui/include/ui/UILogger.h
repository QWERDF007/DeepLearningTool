#pragma once

#include "UIExport.h"
#include "common/Singleton.h"

#include <spdlog/sinks/base_sink.h>

#include <QQueue>

namespace spdlog::sinks {
template<typename Mutex>
class qt_sink : public base_sink<Mutex>
{
public:
    qt_sink(QObject *qt_object)
        : qt_object_(qt_object)
    {
        if (!qt_object_)
        {
            throw_spdlog_ex("qt_sink: qt_object is null");
        }
    }

    ~qt_sink()
    {
        flush_();
    }

protected:
    void sink_it_(const details::log_msg &msg) override
    {
        memory_buf_t formatted;
        base_sink<Mutex>::formatter_->format(msg, formatted);
        const string_view_t str = string_view_t(formatted.data(), formatted.size());
        QMetaObject::invokeMethod(
            qt_object_, "log", Qt::AutoConnection, Q_ARG(int, msg.level),
            Q_ARG(QString, QString::fromUtf8(str.data(), static_cast<int>(str.size())).trimmed()));
    }

    void flush_() override {}

private:
    QObject *qt_object_ = nullptr;
};
} // namespace spdlog::sinks

namespace dltool::ui {

class UI_API UILogger : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(UILogger)
    QT_QML_SINGLETON(UILogger)
    Q_PROPERTY(QString message READ getColorfulMessage NOTIFY messageChanged)
public:
    QString getMessage() const;
    QString getColorfulMessage() const;

    Q_INVOKABLE void log(const int level, const QString &message);

private:
    explicit UILogger(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~UILogger() {}

    QQueue<std::pair<int, QString>> queue_;

    const int max_size_ = 100;

signals:
    void messageChanged();
};

} // namespace dltool::ui
