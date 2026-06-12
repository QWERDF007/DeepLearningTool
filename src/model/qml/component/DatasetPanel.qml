import QtQuick
import QtQuick.Layouts
import dltool.ui

Rectangle {
    id: control

    property int partSpacing: 5
    property int scrollbarReserve: 8

    radius: 4
    clip: true
    color: DltColor.Primary

    DltScrollablePage {
        anchors.fill: parent
        anchors.leftMargin: control.partSpacing
        anchors.topMargin: control.partSpacing
        anchors.rightMargin: 0
        anchors.bottomMargin: control.partSpacing
        animationEnabled: false

        DltText {
            Layout.fillWidth: true
            Layout.rightMargin: control.scrollbarReserve
            text: qsTr("Training Dataset")
            color: DltColor.FontPrimary
            font: DltFont.Title
            elide: Text.ElideRight
        }

        DltText {
            Layout.fillWidth: true
            Layout.rightMargin: control.scrollbarReserve
            text: qsTr("No dataset selected")
            color: DltColor.FontDark
            wrapMode: Text.Wrap
        }
    }
}
