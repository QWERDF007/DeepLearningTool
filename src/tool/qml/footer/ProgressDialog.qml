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
    height: 320
    maskVisible: false
    bg.color: QuiColor.Primary
    bg.border.width: 1
    bg.border.color: "black"
    Item {
        anchors.fill: parent
        anchors.margins: 10
        
        // 标题栏
        RowLayout {
            id: titleBar
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
            }
            spacing: 10
            
            // 标题文本
            QuiText {
                text: "处理进度"
                font: QuiFont.Body
                Layout.fillWidth: true
            }
            
            // 最小化按钮
            QuiTextIconButton {
                id: minimizeBtn
                iconSource: QuiFontIcon.ChromeMinimize
                onClicked: {
                    popup.close()
                }
            }
            
            // 关闭按钮
            QuiTextIconButton {
                id: closeBtn
                iconSource: QuiFontIcon.ChromeClose
                enabled: !ProgressManager.isRunning
                onClicked: {
                    ProgressManager.reset()
                    popup.close()
                }
            }
        }
        
        // 分隔线
        Rectangle {
            id: line
            anchors {
                top: titleBar.bottom
                topMargin: 10
            }
            width: parent.width
            height: 1
            color: "black"
        }
        
        // 进度条
        QuiProgressBar {
            id: progressBar
            anchors {
                top: line.bottom
                topMargin: 15
                left: parent.left
                right: parent.right
            }
            value: ProgressManager.progress / 100.0
            textVisible: true
        }
        
        // 消息区域
        Flickable {
            id: messageFlickable
            anchors {
                top: progressBar.bottom
                topMargin: 15
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            contentWidth: width
            contentHeight: messageArea.contentHeight
            clip: true
            
            ScrollBar.vertical: QuiScrollBar {
                policy: ScrollBar.AsNeeded
            }
            
            QuiTextArea {
                id: messageArea
                width: parent.width
                height: Math.max(parent.height, contentHeight)
                readOnly: true
                text: ProgressManager.message
                wrapMode: Text.Wrap
                background: Rectangle {
                    color: QuiColor.Background
                    border.width: 1
                    border.color: "black"
                }
            }
        }
    }
}
