import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.model
import quickui

Rectangle {
    id: modelView
    width: 200
    height: 200
    color: QuiColor.Primary
    property alias headerTitle: header.text
    property alias addEnable: header.addEnable
    property ModelManager modelManager
    property var currentModelId: -1
    property string currentModelUuid: ""
    property string currentModelName: ""
    property string currentNetworkStructure: ""
    property bool componentCompleted: false
    property var taskManager: null
    property string taskType: ""
    property bool taskActionsEnabled: false

    function clearCurrentModel() {
        currentModelId = -1
        currentModelUuid = ""
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
        if (!modelData || modelData.model_id === undefined || modelData.model_id < 0 || !modelData.uuid) {
            clearCurrentModel()
            return
        }

        if (view.currentIndex !== row) {
            view.currentIndex = row
        }
        currentModelId = modelData.model_id
        currentModelUuid = modelData.uuid || ""
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

    function startCurrentModelTask() {
        if (!taskActionsEnabled || !taskManager || currentModelUuid.length === 0) {
            return
        }
        taskManager.startModelTask(currentModelUuid, currentModelName, taskType)
    }

    function stopCurrentModelTask() {
        if (!taskActionsEnabled || !taskManager || currentModelUuid.length === 0) {
            return
        }
        taskManager.stopModelTask(currentModelUuid, taskType)
    }

    onModelManagerChanged: requestEnsureCurrentModel()
    Component.onCompleted: {
        componentCompleted = true
        requestEnsureCurrentModel()
    }

    QuiMenu {
        id: modelMenu
        width: 160
        QuiMenuItem {
            text: "重命名"
            iconSource: QuiFontIcon.Rename
            onClicked: {
                if (modelView.currentModelId < 0)
                    return
                renameEditor.text = modelView.currentModelName
                renameEditor.open()
            }
        }
        QuiMenuItem {
            text: "删除"
            iconSource: QuiFontIcon.Delete
            onClicked: {
                if (modelManager && modelView.currentModelId >= 0) {
                    modelManager.deleteModel(modelView.currentModelId)
                    Qt.callLater(modelView.ensureCurrentModel)
                }
            }
        }
        QuiMenuItem {
            text: "复制"
            iconSource: QuiFontIcon.Copy
            onClicked: {
                if (modelManager && modelView.currentModelId >= 0) {
                    modelManager.copyModel(modelView.currentModelId)
                }
            }
        }
    }

    QuiEditor {
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
                ScrollBar.vertical: QuiScrollBar {}
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
                    modelUuid: model.uuid
                    modelName: model.name
                    networkStructure: model.network_structure
                    trainingResult: model.training_result
                    testResult: model.test_result
                    selected: ListView.isCurrentItem
                    showTaskActions: modelView.taskActionsEnabled
                    taskActionsEnabled: modelView.taskManager !== null && model.uuid
                    onStartClicked: {
                        modelView.selectModel(index)
                        modelView.startCurrentModelTask()
                    }
                    onStopClicked: {
                        modelView.selectModel(index)
                        modelView.stopCurrentModelTask()
                    }

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

            QuiText {
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: QuiColor.FontDark
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
