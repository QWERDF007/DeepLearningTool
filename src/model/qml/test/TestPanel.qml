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
    property ModelTestTaskManager testTaskManager: null
    property string currentModelUuid: ""
    property IModel selectedModel: modelManager && currentModelUuid.length > 0
                                   ? modelManager.modelForUuid(currentModelUuid)
                                   : null
    property ITestParams testParams: testTaskManager ? testTaskManager.currentTestParams
                                                       : (selectedModel && selectedModel.config ? selectedModel.config.testParams : null)

    // Evaluation is lazy: it starts only when this page is visible, a model is
    // selected and the evaluation section is expanded.  Results stay cached in
    // memory; parameter changes and manual refresh re-evaluate.
    function requestLazyEvaluation() {
        if (!testPanel.visible || testPanel.currentModelUuid.length === 0 || !evaluationExpander.expand)
            return
        var evaluation = testPanel.testTaskManager ? testPanel.testTaskManager.currentEvaluation : null
        if (evaluation)
            evaluation.evaluate(false)
    }

    onVisibleChanged: requestLazyEvaluation()
    onCurrentModelUuidChanged: requestLazyEvaluation()

    ColumnLayout {
        anchors.fill: parent
        spacing: 5

        TestTaskPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.minimumHeight: 48
            Layout.maximumHeight: 48
            taskManager: testPanel.testTaskManager
        }

        QuiExpander {
            id: settingsExpander
            Layout.fillWidth: true
            Layout.preferredHeight: expand ? parent.height / 2 - 96 : headerHeight

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
                    selectionModel: testPanel.testTaskManager
                                    ? testPanel.testTaskManager.currentDatasetViewModel : null
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
            onExpandChanged: testPanel.requestLazyEvaluation()

            TestEvaluationPanel {
                anchors.fill: parent
                evaluation: testPanel.testTaskManager ? testPanel.testTaskManager.currentEvaluation : null
            }
        }
    }

}
