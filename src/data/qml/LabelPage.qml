import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.project

import "label"
import "gallery"
import "component"

Rectangle {
    id: labelPage
    width: 1080
    height: 1920
    color: DltColor.Background

    property DataManager dataManager

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
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: DltColor.Primary
                dataManager: labelPage.dataManager
            }

            LabelImageFlip { // 图像切换
                SplitView.fillWidth: true
                SplitView.minimumHeight: 120
                SplitView.preferredHeight: 120
                color: DltColor.Primary
                dataManager: labelPage.dataManager
            }

            LabelClassesView { // 标签类别
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: DltColor.Primary
                dataManager: labelPage.dataManager
            }

            ImageTagView { // 图像标签
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: DltColor.Primary
                multiSelect: false
                dataManager: labelPage.dataManager
            }
        }

        LabelCanvas { // 标注画布
            id: labelCanvas
            SplitView.fillHeight: true
            SplitView.fillWidth: true
            dataManager: labelPage.dataManager
        }

        DltSplitView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 320
            orientation: Qt.Vertical

            ImageEnhancementPanel { // 图像增强
                id: imageEnhancementPanel
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 200
                
                // 绑定当前图像缩放值
                zoomValue: labelCanvas.imageScale
                
                onFitToWindow: {
                    labelCanvas.fitImageInView()
                }
                
                onZoomChanged: function(zoom) {
                    labelCanvas.setImageScale(zoom)
                }
            }

            LabelsTableView { // 标注实例
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 240
                dataManager: labelPage.dataManager
            }
            
            Rectangle { // 编辑实例
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 4 - 20
                color: DltColor.Primary
            }

            FileListView { // 文件列表
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 4 - 20
                dataManager: labelPage.dataManager
            }
        }
    }
}
