#pragma once

#include "common/Singleton.h"
#include "dltool/ui/Export.h"

#include <QItemSelectionModel>
#include <QObject>
#include <QString>

namespace dltool::ui {

class UI_API SignalHelper : public QObject
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

public:
    static void notifySuccess(const QString &title, const QString &message = QString(), int duration = 3500)
    {
        emit getInstance()->success(title, message, duration);
    }

    static void notifyInfo(const QString &title, const QString &message = QString(), int duration = 3500)
    {
        emit getInstance()->info(title, message, duration);
    }

    static void notifyWarn(const QString &title, const QString &message = QString(), int duration = 0)
    {
        emit getInstance()->warn(title, message, duration);
    }

    static void notifyError(const QString &title, const QString &message = QString(), int duration = 0)
    {
        emit getInstance()->error(title, message, duration);
    }

signals:
    void success(const QString &title, const QString &message = QString(), int duration = 3500);
    void info(const QString &title, const QString &message = QString(), int duration = 3500);
    void warn(const QString &title, const QString &message = QString(), int duration = 0);
    void error(const QString &title, const QString &message = QString(), int duration = 0);

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
};

} // namespace dltool::ui
