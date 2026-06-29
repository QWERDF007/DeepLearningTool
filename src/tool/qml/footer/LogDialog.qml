import QtQuick
import QtQuick.Controls
import QtQuick.Window

import dltool.ui
import quickui

Window {
    id: dialog
    visible: false
    modality: Qt.NonModal
    flags: Qt.Window | Qt.CustomizeWindowHint | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMaximizeButtonHint | Qt.WindowCloseButtonHint
    color: QuiColor.Primary
    title: "日志"
    width: 640
    height: 320
    minimumWidth: 360
    minimumHeight: 220

    function open() {
        show()
        raise()
        requestActivate()
    }

    onVisibilityChanged: {
        if (visibility === Window.Minimized)
            close()
    }

    QuiMenu {
        id: copyMenu
        width: 150

        QuiMenuItem {
            text: "复制"
            iconSource: QuiFontIcon.Copy
            enabled: textArea.selectedText.length > 0
            onTriggered: textArea.copy()
        }
    }

    Flickable {
        id: logFlickable
        anchors.fill: parent
        anchors.margins: 10
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: textArea.height

        ScrollBar.vertical: QuiScrollBar {
            policy: ScrollBar.AsNeeded
        }

        QuiTextArea {
            id: textArea
            readOnly: true
            selectByMouse: true
            width: logFlickable.width
            height: Math.max(logFlickable.height, contentHeight)
            textFormat: Text.AutoText
            text: UILogger.message
            wrapMode: Text.Wrap

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: function(mouse) {
                    let pos = mapToItem(null, mouse.x, mouse.y)
                    textArea.forceActiveFocus()
                    copyMenu.x = pos.x
                    copyMenu.y = pos.y
                    copyMenu.popup()
                }
            }
        }
    }
}
