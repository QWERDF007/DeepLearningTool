import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data

import "gallery"
import "component"
import quickui

Rectangle {
    id: galleryPage
    width: 1080
    height: 1920
    color: QuiColor.Background

    property DataManager dataManager
    
    QuiSplitView {
        anchors.fill: parent
        anchors.margins: 5

        QuiSplitView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 300
            orientation: Qt.Vertical
            DatasetsView { // 数据集
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
                color: QuiColor.Primary
                dataManager: galleryPage.dataManager
            }
            ImageInstanceInfo { // 图像属性
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: QuiColor.Primary
                dataManager: galleryPage.dataManager
            }
            ImageInstancesSelection {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 100
                SplitView.maximumHeight: 100
                SplitView.preferredHeight: 100
                color: QuiColor.Primary
                dataManager: galleryPage.dataManager
            }
            ImageTagView { // 图像标签
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: QuiColor.Primary
                multiSelect: true
                dataManager: galleryPage.dataManager
            }
        }

        RowLayout {
            SplitView.fillHeight: true
            SplitView.fillWidth: true

            GalleryView { // 图像列表
                id: imagesView
                Layout.fillWidth: true
                Layout.fillHeight: true
                dataManager: galleryPage.dataManager
            }

            GallerySidebar { // 侧边栏
                Layout.preferredWidth: 40
                Layout.fillHeight: true
            }
        }
    }
}
