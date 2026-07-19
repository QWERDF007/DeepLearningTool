import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.model
import dltool.project
import "component"
import quickui

Rectangle {
    id: labelPage
    width: 1080
    height: 1920
    color: QuiColor.Background

    property ModelManager modelManager: ProjectManager.currentProject ? ProjectManager.currentProject.modelManager : null
    property ModelTaskController modelTaskController: ProjectManager.currentProject ? ProjectManager.currentProject.modelTaskController : null
    property DataManager dataManager: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager : null
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
            taskManager: TaskManager
            taskController: labelPage.modelTaskController
            taskType: ModelTaskTypes.Test
            taskActionsEnabled: true
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
            property ITestParams testParams: selectedModel && selectedModel.config ? selectedModel.config.testParams : null

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Item {
                    Layout.fillHeight: true
                    Layout.preferredWidth: Math.max(260, Math.floor(testPanel.width * 0.32))
                    Layout.minimumWidth: 220

                    DatasetPanel {
                        anchors.fill: parent
                        roleTitle: qsTr("测试数据集")
                        dataManager: labelPage.dataManager
                        selectionModel: testPanel.selectedModel ? testPanel.selectedModel.testDatasetViewModel : null
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    QuiText {
                        anchors.centerIn: parent
                        width: Math.max(parent.width - 32, 0)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: QuiColor.FontDark
                        text: modelView.currentModelUuid.length > 0 ? qsTr("暂无测试参数") : qsTr("请选择模型")
                        visible: !testPanel.testParams || testPanel.testParams.count <= 0
                    }

                    RowLayout {
                        anchors.fill: parent
                        visible: testPanel.testParams && testPanel.testParams.count > 0
                        spacing: 5

                        Repeater {
                            model: 2

                            ParamPanel {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumWidth: 0
                                params: testPanel.testParams
                                targetPartIndex: index
                                partSpacing: 5
                            }
                        }
                    }
                }
            }
        }
    }
}
