import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    width: 1080
    height: 1920
    color: DltColor.Background
    DltSplitView {
        anchors.fill: parent
        anchors.margins: 5

        DltSplitView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 300
            orientation: Qt.Vertical
            Rectangle { // 数据集
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
                color: DltColor.Primary
            }
            Rectangle { // 图像属性
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 360
                color: DltColor.Primary
            }
            Rectangle { // 图像标签
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: DltColor.Primary
            }
        }

        Rectangle {
            SplitView.fillHeight: true
            SplitView.fillWidth: true
            color: DltColor.Background

            Rectangle {
                anchors.right: parent.right
                height: parent.height
                width: 48
                color: DltColor.Primary
            }
        }
    }
}
