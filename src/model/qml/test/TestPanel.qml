import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import dltool.ui
import dltool.data
import dltool.model
import quickui

Item {
    id: testPanel

    property ModelManager modelManager: null
    property DataManager dataManager: null
    property ModelTaskController modelTaskController: null
    property ModelTestTaskManager testTaskManager: null
    property string currentModelUuid: ""
    property IModel selectedModel: modelManager && currentModelUuid.length > 0
                                   ? modelManager.modelForUuid(currentModelUuid)
                                   : null
    property ITestParams testParams: testTaskManager ? testTaskManager.currentTestParams
                                                       : (selectedModel && selectedModel.config ? selectedModel.config.testParams : null)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TestTaskPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.minimumHeight: 48
            Layout.maximumHeight: 48
            taskManager: testPanel.testTaskManager
            modelTaskController: testPanel.modelTaskController
            modelUuid: testPanel.currentModelUuid
        }

        QuiExpander {
            id: settingsExpander
            Layout.fillWidth: true
            Layout.preferredHeight: expand ? parent.height / 2 - 48 : headerHeight

            headerText: qsTr("设置")
            expand: true
            contentHeight: Math.max(0, height - headerHeight)

            RowLayout {
                id: settingsForm

                anchors.fill: parent
                anchors.topMargin: 10
                spacing: 5

                TestDatasetPanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 0
                    dataManager: testPanel.dataManager
                    selectedModel: testPanel.selectedModel
                }

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
        QuiExpander {
            id: evaluationExpander

            Layout.fillWidth: true
            Layout.fillHeight: true
            headerText: qsTr("评估")
            expand: true
            contentHeight: Math.max(0, height - headerHeight)

            TestEvaluationPanel {
                anchors.fill: parent
                evaluation: testPanel.testTaskManager ? testPanel.testTaskManager.currentEvaluation : null
            }
        }
    }

}
