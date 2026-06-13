import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

Rectangle {
    id: control
    width: 20
    height: 20
    property bool checked: false
    property bool hovered: mouseArea.containsMouse
    property color normalColor: checked ? QuiColor.Highlight : QuiColor.Primary
    property color hoverColor: Qt.lighter(normalColor, 1.2)
    color: hovered ? hoverColor : normalColor

    RowLayout {
        anchors.fill: parent
        QuiText {
            Layout.leftMargin: 5
            text: "日志"
            verticalAlignment: Text.AlignVCenter
        }
        QuiInfoBadge {
            Layout.preferredWidth: 32
            count: UILogger.infoCount
            max: 99
        }
        QuiInfoBadge {
            Layout.preferredWidth: 32
            count: UILogger.errorCount
            max: 99
            icon.iconSource: QuiFontIcon.ErrorBadge
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