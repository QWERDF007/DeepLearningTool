import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

Rectangle {
    id: sidebar
    color: QuiColor.Primary
    border.color: QuiColor.Border

    property bool hasSelection: false
    property bool showBoundingBoxes: false

    signal deleteSelected()
    signal toggleBoundingBoxes()
    signal copySelected()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 6

        QuiTextIconButton {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            iconSource: QuiFontIcon.Delete
            text: "删除"
            enabled: sidebar.hasSelection
            onClicked: sidebar.deleteSelected()
        }

        QuiTextIconButton {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            normalColor: sidebar.showBoundingBoxes ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.View
            text: "显示外接矩形"
            onClicked: sidebar.toggleBoundingBoxes()
        }

        QuiTextIconButton {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            iconSource: QuiFontIcon.Copy
            text: "复制"
            enabled: sidebar.hasSelection
            onClicked: sidebar.copySelected()
        }

        Item {
            Layout.fillWidth: true
        }
    }
}
