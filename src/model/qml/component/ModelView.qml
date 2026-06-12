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
    property string currentNetworkStructure: ""
    property bool componentCompleted: false

    function clearCurrentModel() {
        currentModelId = -1
        currentModelName = ""
        currentNetworkStructure = ""
        if (view.currentIndex !== -1) {
            view.currentIndex = -1
        }
    }

    function selectModel(row) {
        if (!modelManager || row < 0 || row >= view.count) {
            clearCurrentModel()
            return
        }

        const modelData = modelManager.modelAt(row)
        if (!modelData || modelData.model_id === undefined || modelData.model_id < 0) {
            clearCurrentModel()
            return
        }

        if (view.currentIndex !== row) {
            view.currentIndex = row
        }
        currentModelId = modelData.model_id
        currentModelName = modelData.name || ""
        currentNetworkStructure = modelData.network_structure || ""
    }

    function ensureCurrentModel() {
        if (!modelManager || view.count <= 0) {
            clearCurrentModel()
            return
        }

        const row = view.currentIndex >= 0 && view.currentIndex < view.count ? view.currentIndex : 0
        selectModel(row)
    }

    function requestEnsureCurrentModel() {
        if (componentCompleted) {
            Qt.callLater(ensureCurrentModel)
        }
    }

    onModelManagerChanged: requestEnsureCurrentModel()
    Component.onCompleted: {
        componentCompleted = true
        requestEnsureCurrentModel()
    }

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
                    Qt.callLater(modelView.ensureCurrentModel)
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
                onCountChanged: modelView.requestEnsureCurrentModel()
                onModelChanged: modelView.requestEnsureCurrentModel()
                onCurrentIndexChanged: {
                    if (currentIndex >= 0) {
                        Qt.callLater(function () {
                            modelView.selectModel(view.currentIndex)
                        })
                    } else if (count <= 0) {
                        modelView.clearCurrentModel()
                    }
                }

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
                            modelView.selectModel(index)
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
