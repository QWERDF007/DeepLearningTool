import QtQuick
import QtQuick.Layouts
import dltool.ui
import quickui

Rectangle {
    id: control

    property int partSpacing: 5
    property int scrollbarReserve: 8

    radius: 4
    clip: true
    color: QuiColor.Primary

    QuiScrollablePage {
        anchors.fill: parent
        anchors.leftMargin: control.partSpacing
        anchors.topMargin: control.partSpacing
        anchors.rightMargin: 0
        anchors.bottomMargin: control.partSpacing
        animationEnabled: false

        QuiText {
            Layout.fillWidth: true
            Layout.rightMargin: control.scrollbarReserve
            text: qsTr("Training Dataset")
            color: QuiColor.FontPrimary
            font: QuiFont.Title
            elide: Text.ElideRight
        }

        QuiText {
            Layout.fillWidth: true
            Layout.rightMargin: control.scrollbarReserve
            text: qsTr("No dataset selected")
            color: QuiColor.FontDark
            wrapMode: Text.Wrap
        }
    }
}
