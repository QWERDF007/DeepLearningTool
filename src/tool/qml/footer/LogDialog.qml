import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

import dltool.ui
import quickui

QuiPopup {
    id: popup
    modal: false
    width: 480
    height: 280
    maskVisible: false
    bg.color: QuiColor.Primary
    bg.border.width: 1
    bg.border.color: "black"
    Item {
        anchors.fill: parent
        anchors.margins: 10
        QuiTextIconButton {
            id: closeBtn
            anchors{
                top: parent.top
                right: parent.right
            }
            iconSource: QuiFontIcon.ChromeMinimize
            onClicked: {
                popup.close()
            }
        }
        Rectangle {
            id: line
            anchors{
                top: closeBtn.bottom
                topMargin: 10
            }
            width: parent.width
            height: 1
            color: "black"
        }
        Flickable{
            clip: true
            anchors{
                top: line.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            ScrollBar.vertical: QuiScrollBar {
                // snapMode: ScrollBar.SnapAlways
            }
            boundsBehavior: Flickable.StopAtBounds
            contentHeight: textArea.height
            QuiTextArea {
                id: textArea
                readOnly: true
                width: parent.width
                textFormat: Text.AutoText
                text: UILogger.message
                wrapMode: Text.Wrap
            }
        }
    }
}
