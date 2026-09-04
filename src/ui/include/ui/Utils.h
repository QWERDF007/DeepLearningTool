#pragma once

#include "common/Singleton.h"
#include "dltool/ui/Export.h"

#include <QColor>
#include <QUrl>
#include <QVariant>

namespace dltool::ui {

class UI_API Utils : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Utils)
    QT_QML_SINGLETON(Utils)
public:
    Q_INVOKABLE QColor   withOpacity(const QColor &color, qreal opacity) const;
    Q_INVOKABLE QString  getCleanPath(const QString &path) const;
    Q_INVOKABLE QString  getCleanPath(const QUrl &url) const;
    Q_INVOKABLE QString  toFileUrl(const QString &path) const;
    Q_INVOKABLE QString  toFileUrl(const QUrl &url) const;
    Q_INVOKABLE void     openInFileExplorer(const QString &path);
    Q_INVOKABLE QString  stringValue(const QVariant &value) const;
    Q_INVOKABLE double   numberValue(const QVariant &value, double fallback_value) const;
    Q_INVOKABLE bool     boolValue(const QVariant &value, bool fallback_value) const;
    Q_INVOKABLE bool     isIntegerValueType(const QString &value_type) const;
    Q_INVOKABLE QVariant valueRangeAt(const QVariant &value_range, int index, const QVariant &fallback_value) const;
    Q_INVOKABLE int paramDecimals(const QString &value_type, const QVariant &value_range, const QVariant &value = {},
                                  const QVariant &default_value = {}) const;
    Q_INVOKABLE QVariantList recommendedLabelColors() const;
    Q_INVOKABLE QString      nextRecommendedColor(const QVariant &used_colors) const;

private:
    explicit Utils(QObject *parent = nullptr);
    ~Utils();
};

} // namespace dltool::ui
