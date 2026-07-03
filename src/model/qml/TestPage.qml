import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.model
import dltool.project
import quickui

Rectangle {
    id: labelPage
    width: 1080
    height: 1920
    color: QuiColor.Background

    property ModelManager modelManager: ProjectManager.currentProject ? ProjectManager.currentProject.modelManager : null
    QuiSplitView {
        anchors.fill: parent
        anchors.margins: 5

        ModelView {
            id: modelView

            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.preferredWidth: 300
            SplitView.maximumWidth: parent.width / 2
            headerTitle: "模型测试:"
            addEnable: false
            modelManager: labelPage.modelManager
        }

        Rectangle {
            id: testPanel

            SplitView.fillHeight: true
            SplitView.fillWidth: true
            color: QuiColor.Primary
            radius: 4
            border.color: QuiColor.Border

            property IModel selectedModel: labelPage.modelManager && modelView.currentModelUuid.length > 0
                                           ? labelPage.modelManager.modelForUuid(modelView.currentModelUuid)
                                           : null

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                DatasetSelectionTreeView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 240
                    roleTitle: qsTr("测试数据集/类别")
                    selectionModel: testPanel.selectedModel ? testPanel.selectedModel.testDatasetViewModel : null
                    treeHeight: 190
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }
}
