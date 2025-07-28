import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    width: 640
    height: 36
    color: DltColor.Background
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
            LogInfoBadge {
                id: infoBadge
                anchors.fill: parent
                onCheckedChanged: {
                    let pos = infoBadge.mapToItem(null, 0, 0)
                    log.x = pos.x - log.width + 60
                    log.y = pos.y - log.height - 20
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
    }
}
