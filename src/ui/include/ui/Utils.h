#pragma once

#include "UIExport.h"
#include "common/Singleton.h"

namespace dltool::ui {

class UI_API Utils : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Utils)
    QT_QML_SINGLETON(Utils)
public:
    Q_INVOKABLE QColor  withOpacity(const QColor &color, qreal opacity) const;
    Q_INVOKABLE QString getCleanPath(const QString &path) const;
    Q_INVOKABLE void    openInFileExplorer(const QString &path);

private:
    explicit Utils(QObject *parent = nullptr);
    ~Utils();
};

} // namespace dltool::ui
