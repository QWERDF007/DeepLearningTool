#include "ui/Color.h"

namespace dltool::ui {

dltool::ui::DltColor::DltColor(QObject *parent)
    : QObject(parent)
{
    Background(QColor(48, 48, 48, 255));          // #303030
    Primary(QColor(61, 61, 61, 255));             // #3D3D3D
    Border(QColor(62, 62, 62, 255));              // #3E3E3E
    ScrollBar(QColor(98, 98, 98, 255));           // #626262
    ScrollBarBackground(QColor(45, 45, 45, 255)); // #2D2D2D
    ToolTip(QColor(93, 93, 93, 255));             // #5D5D5D
    Hovered(QColor(95, 95, 95, 255));             // #5F5F5F
    Highlight(QColor("#009688"));
    // Sidebar(QColor(78,78,78, 255)); // #4E4E4E
    // Sidebar(QColor(85,85,85, 255)); // #555555
    FontPrimary(QColor(245, 245, 245, 255));
    FontDark(QColor(144, 144, 144, 255));  // #909090
    TabButton(QColor(80, 80, 80, 255));    // #505050
    Button(QColor(80, 80, 80, 255));       // #505050
    ButtonShadow(QColor(72, 72, 72, 255)); // #484848
    Gray110(QColor(110, 110, 110, 255));   // #6E6E6E
}

DltColor::~DltColor() {}

} // namespace dltool::ui
