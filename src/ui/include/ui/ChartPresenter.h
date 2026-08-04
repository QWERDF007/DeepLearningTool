#pragma once

#include "common/Singleton.h"
#include "dltool/ui/Export.h"

#include <QObject>
#include <QString>
#include <QVariant>

namespace dltool::ui {

/**
 * @brief Generic adapter for data/configuration crossing from C++ models to
 *        JavaScript-backed chart controls.
 *
 * Business-specific chart data belongs to the owning model.  This class only
 * performs presentation-independent QVariant/JavaScript preparation and
 * applies the common UI font colors to Chart.js options.
 */
class UI_API ChartPresenter : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ChartPresenter)
    QT_QML_SINGLETON(ChartPresenter)

public:
    Q_INVOKABLE QVariantMap prepareData(const QVariant &chart_data) const;
    Q_INVOKABLE QVariantMap prepareOptions(const QVariant &options, const QString &font_color = {}) const;

private:
    explicit ChartPresenter(QObject *parent = nullptr);
    ~ChartPresenter();
};

} // namespace dltool::ui
