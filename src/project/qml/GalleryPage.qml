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
                color: DltColor.Primary
            }
            ImageInstanceInfo { // 图像属性
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: DltColor.Primary
            }
            ImageInstancesSelection {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 120
                SplitView.maximumHeight: 160
                SplitView.preferredHeight: 120
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

            ImageInstancesView { // 图像列表
                id: imagesView
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            GallerySidebar { // 侧边栏
                Layout.preferredWidth: 40
                Layout.fillHeight: true
            }
        }
    }
}
