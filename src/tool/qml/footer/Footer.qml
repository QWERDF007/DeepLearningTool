import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import dltool.ui
import quickui

Rectangle {
    width: 640
    height: 36
    color: QuiColor.Background
    
    function calculatePopupDialogPosition(badge, dialog, avoidLogDialog) {
        let pos = badge.mapToItem(null, 0, 0)
        let dialogX = pos.x - dialog.width + 60
        let dialogY = pos.y - dialog.height - 20

        let windowItem = badge.Window.window
        if (avoidLogDialog && log.visible && windowItem) {
            let logX = log.x - windowItem.x
            if (logX >= 0 && logX <= windowItem.width && dialogX + dialog.width > logX) {
                dialogX = logX - dialog.width - 10
            }
        }

        if (windowItem) {
            let maxX = Math.max(0, windowItem.width - dialog.width)
            let maxY = Math.max(0, windowItem.height - dialog.height)
            dialogX = Math.max(0, Math.min(dialogX, maxX))
            dialogY = Math.max(0, Math.min(dialogY, maxY))
        }

        return Qt.point(dialogX, dialogY)
    }

    function calculateWindowDialogPosition(badge, dialog) {
        let pos = badge.mapToItem(null, 0, 0)
        let windowItem = badge.Window.window
        let windowX = windowItem ? windowItem.x : 0
        let windowY = windowItem ? windowItem.y : 0
        let dialogX = windowX + pos.x - dialog.width + 60
        let dialogY = windowY + pos.y - dialog.height - 20
        let targetScreen = windowItem ? windowItem.screen : null

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
    
    RowLayout {
        anchors.fill: parent
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: QuiColor.Primary
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
                    let position = calculatePopupDialogPosition(progressBadge, progressDialog, true)
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
                    let position = calculateWindowDialogPosition(infoBadge, log)
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
                let position = calculatePopupDialogPosition(progressBadge, progressDialog, true)
                progressDialog.x = position.x
                progressDialog.y = position.y
            }
        }
    }

    ProgressDialog {
        id: progressDialog
        onClosed: {
            progressBadge.checked = false
        }
    }
}
