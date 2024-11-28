import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic

import dltool.ui

Button {
    property bool disabled: false
    property string contentDescription: ""
    property color normalColor: DltColor.Button
    property color hoverColor:  DltColor.Hovered
    property color pressedColor: Qt.lighter(normalColor, 1.3)
    property color textColor: DltColor.FontPrimary
    Accessible.role: Accessible.Button
    Accessible.name: control.text
    Accessible.description: contentDescription
    Accessible.onPressAction: control.clicked()
    id: control
    enabled: !disabled
    verticalPadding: 0
    horizontalPadding:12
    font: DltFont.Body
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1 : 0.3
    background: DltControlBackground {
        implicitWidth: 30
        implicitHeight: 30
        radius: 4
        color:  {
            if (enabled) {
                return  pressed ? pressedColor : hovered ? hoverColor : normalColor
            }
            else {
                normalColor
            }
        }
        shadow: !pressed && enabled
        DltFocusRectangle {
            visible: control.activeFocus
            radius:4
        }
        // DltShadow {
        //     color: "#000000"
        //     radius: 4
        // }
    }
    contentItem: DltText {
        text: control.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font: control.font
        color: control.textColor
    }
}
