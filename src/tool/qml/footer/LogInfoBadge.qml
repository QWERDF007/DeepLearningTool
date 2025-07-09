import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    id: control
    width: 20
    height: 20
    property bool checked: false
    property bool hovered: mouseArea.containsMouse
    property color normalColor: checked ? DltColor.Highlight : DltColor.Primary
    property color hoverColor: Qt.lighter(normalColor, 1.2)
    color: hovered ? hoverColor : normalColor

    RowLayout {
        anchors.fill: parent
        DltText {
            Layout.leftMargin: 5
            text: "日志"
        }
        DltInfoBadge {
            count: UILogger.infoCount
            max: 99
        }
        DltInfoBadge {
            count: UILogger.errorCount
            max: 99
            icon.iconSource: DltFontIcon.ErrorBadge
            contentColor: "red"
        }
        Item {
            Layout.fillWidth: true
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            control.checked = !control.checked
        }
    }
}