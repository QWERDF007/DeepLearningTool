import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

import dltool.ui

TabButton {
    id: control
    font: DltFont.Subtitle
    property alias textColor: content.color
    property color normalColor: DltColor.Primary
    property color hoverColor: DltColor.Hovered
    property color pressedColor: Qt.lighter(normalColor, 1.3)
    contentItem: IconLabel {
        id: content
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display

        icon: control.icon
        text: control.text
        font: control.font
        color: control.palette.brightText
        opacity: enabled ? 1 : 0.3
    }

    background: Rectangle {
        id: bg
        implicitHeight: 48
        opacity: enabled ? 1 : 0.3
        color: enabled ? (control.down ? pressedColor : control.hovered ? hoverColor : normalColor) : normalColor
        radius: 2
    }
}
