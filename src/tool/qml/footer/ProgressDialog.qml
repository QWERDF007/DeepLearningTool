import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import dltool.ui
import quickui

Window {
    id: dialog
    visible: false
    modality: Qt.NonModal
    flags: Qt.Window | Qt.CustomizeWindowHint | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMaximizeButtonHint | Qt.WindowCloseButtonHint
    color: QuiColor.Primary
    title: "处理进度"
    width: 640
    height: 320
    minimumWidth: 360
    minimumHeight: 220

    function open() {
        show()
        raise()
        requestActivate()
    }

    onClosing: {
        if (!ProgressManager.isRunning) {
            ProgressManager.reset()
        }
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        QuiProgressBar {
            Layout.fillWidth: true
            value: ProgressManager.progress / 100.0
            textVisible: true
        }

        Flickable {
            id: messageFlickable
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                width: messageFlickable.width
                height: Math.max(messageFlickable.height, contentHeight)
                textFormat: Text.AutoText
                text: ProgressManager.message
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
}
