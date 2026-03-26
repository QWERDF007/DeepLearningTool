import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.project

import "review"
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

            

            ClassFilterView { // 类别筛选
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: DltColor.Primary
                dataManager: labelPage.dataManager
            }

            Rectangle { // 标注列表
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: DltColor.Primary
                
            }
        }

        RowLayout {
            SplitView.fillHeight: true
            SplitView.fillWidth: true

            LabelInstancesGridView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                dataManager: labelPage.dataManager
            }

            ReviewSidebar { // 侧边栏
                Layout.preferredWidth: 40
                Layout.fillHeight: true
            }
        }
    }
}
