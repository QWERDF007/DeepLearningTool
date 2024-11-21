import QtQuick
import QtQuick.Controls

import dltool.ui

Text {
    id:control
    property alias textColor: control.color
    renderType: Text.NativeRendering
    font: DltFont.Body
    color: DltColor.FontPrimary
}
