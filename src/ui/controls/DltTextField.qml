import QtQuick
import QtQuick.Controls

import dltool.ui

TextField{
    id:control
    property alias textColor: control.color
    property color bgColor: DltColor.Gray110
    color: DltColor.FontPrimary
    font: DltFont.Body
    renderType: Text.NativeRendering
    selectionColor: DltColor.Highlight
    selectedTextColor: color
    placeholderTextColor: DltColor.Gray110
    selectByMouse: true
    hoverEnabled: true
    background: Item {
        implicitWidth: 200
        implicitHeight: 40
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 2
            color: control.focus ? DltColor.Highlight : hovered ? "white" : bgColor
        }
    }
}
