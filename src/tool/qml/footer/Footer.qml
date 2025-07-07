import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    width: 640
    height: 32
    color: DltColor.Background
    RowLayout {
        anchors.fill: parent
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: DltColor.Primary
        }
        Rectangle {
            Layout.preferredWidth: 100
            Layout.fillHeight: true
            color: DltColor.Primary
            DltButton {
                id: btn
                text: "日志"
                checkable: true
                normalColor: checked ? DltColor.Highlight : DltColor.Button
                onClicked: {
                    if (checked)
                    {
                        let pos = btn.mapToItem(Qt.application.activeWindow, 0, 0)
                        log.x = pos.x - log.width + 60
                        log.y = pos.y - log.height - 20
                        log.open()
                    }
                    else
                    {
                        log.close()
                    }
                }
            }
        }
    }

    LogDialog {
        id: log
        width: 640
        height: 320
    }
}
