import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project
import dltool.data
import dltool.feature
import quickui

Rectangle {
    id: labelPage
    width: 1080
    height: 1920
    color: QuiColor.Background

    property DataManager dataManager
    property FeatureManager featureManager

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
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: QuiColor.Primary
                dataManager: labelPage.dataManager
            }

            

            ClassFilterView { // 类别筛选
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: QuiColor.Primary
                dataManager: labelPage.dataManager
            }

            SelectedLabelsInfoPanel { // 所选标注信息
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: QuiColor.Primary
                dataManager: labelPage.dataManager
            }
        }

        RowLayout {
            SplitView.fillHeight: true
            SplitView.fillWidth: true

            LabelInstancesGridView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                dataManager: labelPage.dataManager
                featureManager: labelPage.featureManager
            }

            ReviewSidebar { // 侧边栏
                Layout.preferredWidth: 40
                Layout.fillHeight: true
            }
        }
    }
}
