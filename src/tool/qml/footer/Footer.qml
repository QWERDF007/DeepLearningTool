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
            
            DltInfoBadge {
                anchors{
                    right: parent.right
                    top: parent.top
                }
                count: 10
                max: 99
            }
        }
    }
}