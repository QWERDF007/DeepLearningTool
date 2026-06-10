import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.model

Rectangle {
    id: modelView
    width: 200
    height: 200
    color: DltColor.Primary
    property alias headerTitle: header.text
    property alias addEnable: header.addEnable
    property ModelManager modelManager

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        ModelHeader {
            id: header
            Layout.fillWidth: true
            height: 32
            onAddClicked: {
                if (modelManager) {
                    modelFormDialog.networkStructureModel = modelManager.supportedNetworkStructures()
                    modelFormDialog.openForm()
                }
            }
        }

        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true

            ListView {
                id: view
                anchors.fill: parent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: modelManager
                spacing: 5
                ScrollBar.vertical: DltScrollBar {}

                delegate: ModelDelegate {
                    width: view.width
                    modelId: model.model_id
                    modelName: model.name
                    networkStructure: model.network_structure
                    trainingResult: model.training_result
                    testResult: model.test_result
                }
            }

            DltText {
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: DltColor.FontDark
                text: modelManager ? "暂无模型" : "请先打开项目"
                visible: modelManager === null || view.count === 0
            }
        }
    }

    ModelFormDialog {
        id: modelFormDialog
        onSubmitted: function (modelName, networkStructure) {
            if (modelManager) {
                modelManager.addModel(modelName, networkStructure)
            }
        }
    }
}
