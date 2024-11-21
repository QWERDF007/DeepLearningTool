#pragma once

#include "Def.h"
#include "common/Singleton.h"

#include <QFont>

namespace dltool::ui {

class DltFont : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DltFont)
    QT_QML_SINGLETON(DltFont)
    Q_PROPERTY_AUTO(QFont, Caption)
    Q_PROPERTY_AUTO(QFont, Body)
    Q_PROPERTY_AUTO(QFont, BodyStrong)
    Q_PROPERTY_AUTO(QFont, Subtitle)
    Q_PROPERTY_AUTO(QFont, Title)
    Q_PROPERTY_AUTO(QFont, TitleLarge)
    Q_PROPERTY_AUTO(QFont, Display)
private:
    explicit DltFont(QObject *parent = nullptr);
    ~DltFont();
};

} // namespace dltool::ui
