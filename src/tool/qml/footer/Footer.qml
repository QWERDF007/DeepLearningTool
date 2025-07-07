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
                text: "日志"
                onClicked: {
                    log.open()
                }
            }
        }
    }
    LogDialog {
        id: log
        width: 640
        height: 240
    }
}