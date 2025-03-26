import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import "label"

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

            Rectangle { // 图像导航
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: DltColor.Primary
            }

            Rectangle { // 标签类别
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
                color: DltColor.Primary
            }

            Rectangle { // 图像切换
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: DltColor.Primary
            }
        }
        LabelCanvas { // 中间内容
            SplitView.fillHeight: true
            SplitView.fillWidth: true
        }

        DltSplitView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 300
            orientation: Qt.Vertical

            Rectangle { // 标注实例
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
                color: DltColor.Primary
            }
            Rectangle { // 文件列表
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 2 - 20
                color: DltColor.Primary
            }
        }

    }
}
