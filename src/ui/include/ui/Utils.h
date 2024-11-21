#pragma once

#include "common/Singleton.h"

namespace dltool::ui {

class Utils : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Utils)
    QT_QML_SINGLETON(Utils)
public:
    Q_INVOKABLE QColor  withOpacity(const QColor &color, qreal opacity) const;
    Q_INVOKABLE QString documentsLocation() const;

private:
    explicit Utils(QObject *parent = nullptr);
    ~Utils();
};

} // namespace dltool::ui
