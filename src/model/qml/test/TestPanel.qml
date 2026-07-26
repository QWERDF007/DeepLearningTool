import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import dltool.ui
import dltool.data
import dltool.model
import quickui

Item {
    id: testPanel

    SplitView.fillHeight: true
    SplitView.fillWidth: true

    property DataManager dataManager: null
    property IModel selectedModel: labelPage.modelManager && modelView.currentModelUuid.length > 0
                                   ? labelPage.modelManager.modelForUuid(modelView.currentModelUuid)
                                   : null
    property ITestParams testParams: selectedModel && selectedModel.config ? selectedModel.config.testParams : null


    QuiExpander {
        id: settingsExpander
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: expand ? parent.height / 2 : headerHeight

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

        anchors.top: settingsExpander.bottom
        anchors.topMargin: 5
        anchors.left: parent.left
        anchors.right: parent.right

        headerText: qsTr("评估")
        expand: true
        contentHeight: Math.max(0, height - headerHeight)

        TestEvaluationPanel {
            anchors.fill: parent
        }
    }
}
