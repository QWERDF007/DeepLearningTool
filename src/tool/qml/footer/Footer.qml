import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import dltool.ui

Rectangle {
    width: 640
    height: 36
    color: DltColor.Background
    
    // 辅助函数：计算对话框位置并确保在可见区域内
    function calculateDialogPosition(badge, dialog, isProgressDialog) {
        let pos = badge.mapToItem(null, 0, 0)
        let dialogX = pos.x - dialog.width + 60
        let dialogY = pos.y - dialog.height - 20
        
        // 如果是 ProgressDialog，需要确保它在 LogDialog 的左边
        if (isProgressDialog && log.visible) {
            // ProgressDialog 应该在 LogDialog 左边，留出间隙
            let logRightEdge = log.x + log.width
            if (dialogX + dialog.width > log.x) {
                dialogX = log.x - dialog.width - 10  // 10px 间隙
            }
        }
        
        // 边界检查：确保对话框在窗口可见区域内
        let windowItem = badge.Window.window
        if (windowItem) {
            // 左边界检查
            dialogX = Math.max(0, dialogX)
            
            // 右边界检查
            dialogX = Math.min(dialogX, windowItem.width - dialog.width)
            
            // 上边界检查
            dialogY = Math.max(0, dialogY)
            
            // 下边界检查（虽然对话框在 footer 上方，但仍需检查）
            dialogY = Math.min(dialogY, windowItem.height - dialog.height)
        }
        
        return Qt.point(dialogX, dialogY)
    }
    
    RowLayout {
        anchors.fill: parent
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: DltColor.Primary
        }
        Rectangle {
            Layout.preferredWidth: 120
            Layout.fillHeight: true
            color: DltColor.Primary
            ProgressInfoBadge {
                id: progressBadge
                anchors.fill: parent
                onCheckedChanged: {
                    let position = calculateDialogPosition(progressBadge, progressDialog, true)
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
            color: DltColor.Primary
            LogInfoBadge {
                id: infoBadge
                anchors.fill: parent
                onCheckedChanged: {
                    let position = calculateDialogPosition(infoBadge, log, false)
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
        width: 640
        height: 320
        onClosed: {
            infoBadge.checked = false
        }
        
        // 当 LogDialog 打开时，如果 ProgressDialog 也打开，重新计算 ProgressDialog 位置
        onVisibleChanged: {
            if (visible && progressDialog.visible) {
                let position = calculateDialogPosition(progressBadge, progressDialog, true)
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
