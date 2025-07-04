import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

import "label"
import "gallery"
import "component"

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
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: DltColor.Primary
            }

            LabelImageFlip { // 图像切换
                SplitView.fillWidth: true
                SplitView.minimumHeight: 120
                SplitView.preferredHeight: 120
                color: DltColor.Primary
            }

            LabelClassesView { // 标签类别
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: DltColor.Primary
            }

            ImageTagView { // 图像标签
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: DltColor.Primary
                multiSelect: false
            }
        }

        LabelCanvas { // 标注画布
            id: labelCanvas
            SplitView.fillHeight: true
            SplitView.fillWidth: true
            Connections {
                target: labelInstancesView
                function onTableSelectionChanged(index, command) {
                    if (labelCanvas.selection) {
                        labelCanvas.selection.select(index, command)
                    }
                }
            }
        }

        DltSplitView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 300
            orientation: Qt.Vertical

            Rectangle { // 图像增强
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: DltColor.Primary
            }

            LabelInstancesView { // 标注实例
                id: labelInstancesView
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 240
                color: DltColor.Primary
                Connections {
                    target: labelCanvas
                    function onListSelectionChanged(index, command) {
                        if (labelInstancesView.selection) {
                            labelInstancesView.selection.select(index, command)
                        }
                    }
                }
            }
            
            Rectangle { // 编辑实例
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 4 - 20
                color: DltColor.Primary
            }

            Rectangle { // 文件列表
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 4 - 20
                color: DltColor.Primary
            }
        }
    }
}
