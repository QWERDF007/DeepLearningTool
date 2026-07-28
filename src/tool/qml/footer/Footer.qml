import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import dltool.ui
import quickui

Rectangle {
    id: footer
    width: 640
    height: 36
    color: QuiColor.Background

    property bool showLabelStatus: false
    property string labelSidebarState: ""
    property string labelImageName: ""
    property string labelDatasetName: ""
    
    function calculateWindowDialogPosition(badge, dialog, avoidLogDialog) {
        let pos = badge.mapToItem(null, 0, 0)
        let windowItem = badge.Window.window
        let windowX = windowItem ? windowItem.x : 0
        let windowY = windowItem ? windowItem.y : 0
        let dialogX = windowX + pos.x - dialog.width + 60
        let dialogY = windowY + pos.y - dialog.height - 20
        let targetScreen = windowItem ? windowItem.screen : null

        if (avoidLogDialog && log.visible && dialogX < log.x + log.width && dialogX + dialog.width > log.x) {
            dialogX = log.x - dialog.width - 10
        }

        if (targetScreen) {
            let screenX = targetScreen.virtualX
            let screenY = targetScreen.virtualY
            let screenWidth = targetScreen.desktopAvailableWidth > 0 ? targetScreen.desktopAvailableWidth : targetScreen.width
            let screenHeight = targetScreen.desktopAvailableHeight > 0 ? targetScreen.desktopAvailableHeight : targetScreen.height
            let maxX = Math.max(screenX, screenX + screenWidth - dialog.width)
            let maxY = Math.max(screenY, screenY + screenHeight - dialog.height)
            dialogX = Math.max(screenX, Math.min(dialogX, maxX))
            dialogY = Math.max(screenY, Math.min(dialogY, maxY))
        }

        return Qt.point(dialogX, dialogY)
    }

    QuiMenu {
        id: menu
        width: 200
        QuiMenuItem {
            text: "复制"
            onTriggered: {
                copyboard.selectAll()
                copyboard.copy()
            }
        }
    }
    TextEdit {
        id: copyboard
        visible: false
    }
    
    RowLayout {
        anchors.fill: parent
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: QuiColor.Primary

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 18
                visible: footer.showLabelStatus

                QuiText {
                    Layout.preferredWidth: 100
                    text: "工具: " + footer.labelSidebarState
                    elide: Text.ElideRight
                }
                QuiText {
                    Layout.preferredWidth: 240
                    text: "数据集: " + footer.labelDatasetName
                    elide: Text.ElideRight
                }
                QuiText {
                    // Layout.fillWidth: true
                    text: "图像: " + footer.labelImageName 
                    elide: Text.ElideRight

                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: {
                            copyboard.text = footer.labelImageName 
                            menu.popup()
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }
        Rectangle {
            visible: ProgressManager ? ProgressManager.isRunning : false
            Layout.preferredWidth: 80
            Layout.fillHeight: true
            color: QuiColor.Primary
            ProgressInfoBadge {
                id: progressBadge
                anchors.fill: parent
                onCheckedChanged: {
                    let position = calculateWindowDialogPosition(progressBadge, progressDialog, true)
                    progressDialog.x = position.x
                    progressDialog.y = position.y
                    if (checked)
                        progressDialog.open()
                    else
                        progressDialog.close()
                }
            }
        }
        Rectangle {
            Layout.preferredWidth: 120
            Layout.fillHeight: true
            color: QuiColor.Primary
            LogInfoBadge {
                id: infoBadge
                anchors.fill: parent
                onCheckedChanged: {
                    let position = calculateWindowDialogPosition(infoBadge, log, false)
                    log.x = position.x
                    log.y = position.y
                    if (checked)
                    {
                        UILogger.clearCount()
                        log.open()
                    }
                    else
                        log.close()
                }
            }
        }
    }

    LogDialog {
        id: log
        transientParent: infoBadge.Window.window
        width: 640
        height: 320

        onVisibleChanged: {
            if (!visible) {
                infoBadge.checked = false
                return
            }

            if (progressDialog.visible) {
                let position = calculateWindowDialogPosition(progressBadge, progressDialog, true)
                progressDialog.x = position.x
                progressDialog.y = position.y
            }
        }
    }

    ProgressDialog {
        id: progressDialog
        transientParent: progressBadge.Window.window

        onVisibleChanged: {
            if (!visible) {
                progressBadge.checked = false
            }
        }
    }
}
