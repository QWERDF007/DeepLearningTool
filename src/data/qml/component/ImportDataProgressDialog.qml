 import QtQuick
import QtQuick.Controls
import QtQuick.Layouts


import dltool.ui

DltContentDialog {
    id: dialog
    implicitHeight: 800
    implicitWidth: 1200
    property string message
    property real value
    contentDelegate: Component {
        ColumnLayout {
            width: parent.width
            DltProgressBar {
                id: _progress
                Layout.fillWidth: true
                Layout.margins: 10
                backgroundColor: DltColor.Primary
                from: 0
                to: 1
                value: dialog.value
            }
            Rectangle {
                implicitHeight: dialog.height - 180
                Layout.fillWidth: true
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                color: DltColor.Primary
                Flickable {
                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true
                    contentHeight: text_message.height
                    contentWidth: width
                    boundsBehavior:Flickable.StopAtBounds
                    ScrollBar.vertical: DltScrollBar {}
                    DltText { // 消息
                        id: text_message
                        font: DltFont.Body
                        wrapMode: Text.WrapAnywhere
                        width: parent.width
                        text: dialog.message
                    }
                }
            }
        }
    }
}
