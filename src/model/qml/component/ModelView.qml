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
    property var currentModelId: -1
    property string currentModelName: ""

    DltMenu {
        id: modelMenu
        width: 160
        DltMenuItem {
            text: "重命名"
            iconSource: DltFontIcon.Rename
            onClicked: {
                if (modelView.currentModelId < 0)
                    return
                renameEditor.text = modelView.currentModelName
                renameEditor.open()
            }
        }
        DltMenuItem {
            text: "删除"
            iconSource: DltFontIcon.Delete
            onClicked: {
                if (modelManager && modelView.currentModelId >= 0) {
                    modelManager.deleteModel(modelView.currentModelId)
                    view.currentIndex = -1
                    modelView.currentModelId = -1
                    modelView.currentModelName = ""
                }
            }
        }
        DltMenuItem {
            text: "复制"
            iconSource: DltFontIcon.Copy
            onClicked: {
                if (modelManager && modelView.currentModelId >= 0) {
                    modelManager.copyModel(modelView.currentModelId)
                }
            }
        }
    }

    DltEditor {
        id: renameEditor
        description: "输入模型名称"
        onEditTextChanged: function (modelName) {
            if (modelManager && modelView.currentModelId >= 0) {
                if (modelManager.renameModel(modelView.currentModelId, modelName)) {
                    modelView.currentModelName = modelName
                }
            }
        }
    }

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
                currentIndex: -1
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
                    selected: ListView.isCurrentItem

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: function (mouse) {
                            view.currentIndex = index
                            modelView.currentModelId = model.model_id
                            modelView.currentModelName = model.name
                            if (mouse.button === Qt.RightButton) {
                                modelMenu.popup()
                            }
                        }
                    }
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
