#include "ui/Font.h"

namespace dltool::ui {

DltFont::DltFont(QObject *parent)
    : QObject(parent)
{
    QFont caption;
    caption.setPixelSize(12);
    Caption(caption);

    QFont body;
    body.setPixelSize(16);
    Body(body);

    QFont body_strong;
    body_strong.setPixelSize(16);
    body_strong.setWeight(QFont::DemiBold);
    BodyStrong(body_strong);

    QFont subtitle;
    subtitle.setPixelSize(20);
    subtitle.setWeight(QFont::DemiBold);
    Subtitle(subtitle);

    QFont title;
    title.setPixelSize(28);
    title.setWeight(QFont::DemiBold);
    Title(title);

    QFont title_large;
    title_large.setPixelSize(40);
    title_large.setWeight(QFont::DemiBold);
    TitleLarge(title_large);

    QFont display;
    display.setPixelSize(68);
    display.setWeight(QFont::DemiBold);
    Display(display);
}

DltFont::~DltFont() {}

} // namespace dltool::ui
