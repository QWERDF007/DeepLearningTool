import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import "gallery"

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
            DatasetsView { // 数据集
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
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

        RowLayout {
            SplitView.fillHeight: true
            SplitView.fillWidth: true

            ImageInstancesView {
                id: imagesView
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            Rectangle {
                Layout.preferredWidth: 48
                Layout.fillHeight: true
                color: DltColor.Primary
            }
        }
    }
}
