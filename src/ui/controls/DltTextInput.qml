import QtQuick
import QtQuick.Controls

import dltool.ui

TextInput{
    id:control
    property color bgColor: DltColor.Gray110
    property alias textColor: control.color
    color: DltColor.FontPrimary
    font: DltFont.Body
    renderType: Text.NativeRendering
    selectionColor: DltColor.Highlight
    selectedTextColor: color
    selectByMouse: true
}
