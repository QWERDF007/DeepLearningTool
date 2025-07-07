#pragma once

#include "UIExport.h"
#include "common/Singleton.h"

#include <QQueue>

namespace dltool::ui {

class UI_API UILogger : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(UILogger)
    QT_QML_SINGLETON(UILogger)
    Q_PROPERTY(QString message READ getMessage NOTIFY messageChanged)
public:
    QString getMessage() const;

    Q_INVOKABLE void log(const QString &message);

private:
    explicit UILogger(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~UILogger()
    {
        qInfo() << __FUNCTION__;
    }

    QQueue<QString> queue_;

    const int max_size_ = 100;

signals:
    void messageChanged();
};

} // namespace dltool::ui
