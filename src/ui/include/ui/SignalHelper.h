#pragma once

#include "common/Singleton.h"

namespace dltool::ui {

class SignalHelper : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SignalHelper)
    QT_QML_SINGLETON(SignalHelper)
private:
    explicit SignalHelper(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~SignalHelper() {}

signals:
    void changeTabBarIndex(int index);

    void imageLabelListSelectionChanged(const QModelIndex &index, QItemSelectionModel::SelectionFlags command);
    void imageLabelListShiftSelect(int currentIndex, int lastIndex, QItemSelectionModel::SelectionFlags command);
    void imageLabelListSelectionClear();
    void imageLabelListSelectAll();
    void imageLabelTableSelectionChanged(const QModelIndex &index, QItemSelectionModel::SelectionFlags command);
    void imageLabelTableShiftSelect(int currentIndex, int lastIndex, QItemSelectionModel::SelectionFlags command);
    void imageLabelTableSelectionClear();
    void imageLabelTableSelectAll();

    // Navigation signals for review-to-label navigation
    void switchToImage(int64_t image_id);
    void selectLabel(int64_t label_id);

    // Filter-related signals
    void filterCriteriaChanged(QString filterType, QVariantList ids);
    void filterModuleEnabledChanged(QString filterType, bool enabled);
    void clearAllFilters();
};

} // namespace dltool::ui
