#pragma once

#include "Def.h"
#include "common/Singleton.h"

#include <QColor>

namespace dltool::ui {

class DltColor : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DltColor)
    QT_QML_SINGLETON(DltColor)
    Q_PROPERTY_AUTO(QColor, Transparent)
    Q_PROPERTY_AUTO(QColor, Background)
    Q_PROPERTY_AUTO(QColor, Primary)
    Q_PROPERTY_AUTO(QColor, Border)
    Q_PROPERTY_AUTO(QColor, ScrollBar)
    Q_PROPERTY_AUTO(QColor, ScrollBarBackground)
    Q_PROPERTY_AUTO(QColor, ToolTip)
    Q_PROPERTY_AUTO(QColor, Hovered)
    Q_PROPERTY_AUTO(QColor, Highlight)
    Q_PROPERTY_AUTO(QColor, FontPrimary)
    Q_PROPERTY_AUTO(QColor, FontDark)
    Q_PROPERTY_AUTO(QColor, TabButton)
    Q_PROPERTY_AUTO(QColor, Button)
    Q_PROPERTY_AUTO(QColor, ButtonShadow)
    Q_PROPERTY_AUTO(QColor, Gray110)
private:
    explicit DltColor(QObject *parent = nullptr);
    ~DltColor();
};

} // namespace dltool::ui
