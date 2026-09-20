import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

Rectangle {
    id: root
    clip: true
    color: QuiColor.Primary

    property string title: ""
    property string actionText: ""
    property string actionIcon: QuiFontIcon.Add
    property bool showActionButton: actionText.length > 0
    property alias headerRightItem: headerRightSlot.data
    property bool headerVisible: true
    property int headerHeight: 32

    property int leftMargin: 5
    property int rightMargin: 0
    property int topMargin: 5
    property int bottomMargin: 5
    property int headerLeftMargin: 0
    property int headerRightMargin: 5
    property int spacing: 5

    signal actionClicked()

    default property alias content: contentContainer.data

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.leftMargin
        anchors.rightMargin: root.rightMargin
        anchors.topMargin: root.topMargin
        anchors.bottomMargin: root.bottomMargin
        spacing: root.spacing

        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            Layout.leftMargin: root.headerLeftMargin
            Layout.rightMargin: root.headerRightMargin
            Layout.preferredHeight: root.headerHeight
            visible: root.headerVisible && (root.title.length > 0 || root.showActionButton || headerRightSlot.children.length > 0)

            QuiText {
                text: root.title
                font: QuiFont.Subtitle
                visible: root.title.length > 0
            }

            Item {
                Layout.fillWidth: true
            }

            Item {
                id: headerRightSlot
                Layout.preferredHeight: root.headerHeight
                Layout.preferredWidth: childrenRect.width
                visible: children.length > 0
            }

            QuiTextIconButton {
                id: actionButton
                visible: root.showActionButton
                iconSource: root.actionIcon
                text: root.actionText
                onClicked: root.actionClicked()
            }
        }

        Item {
            id: contentContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
        }
    }
}
