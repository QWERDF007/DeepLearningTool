import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic

import dltool.ui

Button {
    property bool disabled: false
    property string contentDescription: ""
    property color normalColor: DltColor.Button
    property color hoverColor: Qt.lighter(normalColor, 1.2)
    property color pressedColor: Qt.lighter(normalColor,1.3)
    property color textColor: Qt.rgba(1,1,1,1)
    Accessible.role: Accessible.Button
    Accessible.name: control.text
    Accessible.description: contentDescription
    Accessible.onPressAction: control.clicked()
    id: control
    enabled: !disabled
    focusPolicy:Qt.TabFocus
    font: DltFont.Body
    verticalPadding: 0
    horizontalPadding:12
    opacity: enabled ? 1 : 0.3
    background: DltControlBackground{
        implicitWidth: 30
        implicitHeight: 30
        radius: 4
        bottomMargin: enabled ? 2 : 0
        border.width: enabled ? 1 : 0
        border.color: Qt.darker(control.normalColor,1.2)
        color: pressed ? pressedColor : hovered ? hoverColor : normalColor
        shadow: !pressed && enabled
        DltFocusRectangle{
            visible: control.visualFocus
            radius:4
        }
    }
    contentItem: DltText {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: control.textColor
    }
}
