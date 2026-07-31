import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.feature
import quickui

Rectangle {
    id: galleryPage
    width: 1080
    height: 1920
    color: QuiColor.Background
    focus: true

    property DataManager dataManager
    property FeatureManager featureManager

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: function(event) {
        if (imageTagView.handleShortcutEvent(event)) {
            event.accepted = true
        }
    }
    
    QuiSplitView {
        anchors.fill: parent
        anchors.margins: 5

        QuiSplitView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 260
            orientation: Qt.Vertical
            DatasetsView { // 数据集
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
                color: QuiColor.Primary
                dataManager: galleryPage.dataManager
                featureManager: galleryPage.featureManager
            }
            
        }

        GalleryView { // 图像列表
            id: imagesView
            SplitView.fillHeight: true
            SplitView.fillWidth: true
            dataManager: galleryPage.dataManager
            featureManager: galleryPage.featureManager
        }

        QuiSplitView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 260
            orientation: Qt.Vertical
            GalleryAdjustmentPanel { // 图库显示
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 200
            }
            ImageInstanceInfo { // 图像属性
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 200
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
                id: imageTagView
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 200
                color: QuiColor.Primary
                multiSelect: true
                dataManager: galleryPage.dataManager
            }
        }
    }
}
