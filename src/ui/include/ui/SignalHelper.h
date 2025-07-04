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
    void imageLabelTableSelectionChanged(const QModelIndex &index, QItemSelectionModel::SelectionFlags command);
};

} // namespace dltool::ui
