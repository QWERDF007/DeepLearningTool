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
    property string currentFrameworkName: ""
    property string currentModelArchitecture: ""
    property bool componentCompleted: false
    property var taskManager: null
    property int taskType: ModelTaskTypes.Unknown
    property bool taskActionsEnabled: false
    property int taskRevision: taskManager && taskManager.tasks ? taskManager.tasks.revision : 0

    function resetCurrentModelState() {
        currentModelId = -1
        currentModelUuid = ""
        currentModelName = ""
        currentFrameworkName = ""
        currentModelArchitecture = ""
        if (view.currentIndex !== -1) {
            view.currentIndex = -1
        }
    }

    function selectModel(row) {
        if (!modelManager || row < 0 || row >= view.count) {
            resetCurrentModelState()
            return
        }

        const modelData = modelManager.modelAt(row)
        if (!modelData || modelData.model_id === undefined || modelData.model_id < 0 || !modelData.uuid) {
            resetCurrentModelState()
            return
        }

        if (view.currentIndex !== row) {
            view.currentIndex = row
        }
        currentModelId = modelData.model_id
        currentModelUuid = modelData.uuid || ""
        currentModelName = modelData.name || ""
        currentFrameworkName = modelData.framework_name || ""
        currentModelArchitecture = modelData.model_architecture || ""
    }

    function ensureCurrentModel() {
        if (!modelManager || view.count <= 0) {
            resetCurrentModelState()
            return
        }

        const row = view.currentIndex >= 0 && view.currentIndex < view.count ? view.currentIndex : 0
        selectModel(row)
    }

    function requestEnsureCurrentModel() {
        if (componentCompleted) {
            ensureCurrentModel()
        }
    }

    function startCurrentModelTask() {
        if (!canStartModelTask(currentModelUuid)) {
            return
        }
        taskManager.startModelTask(currentModelUuid, currentModelName, taskType)
    }

    function stopCurrentModelTask() {
        if (!canStopModelTask(currentModelUuid)) {
            return
        }
        taskManager.stopModelTask(currentModelUuid, taskType)
    }

    function addCurrentModelTask() {
        if (!taskActionsEnabled || !taskManager || currentModelUuid.length === 0) {
            return
        }
        taskManager.addModelTask(currentModelUuid, currentModelName, taskType)
    }

    function taskForModel(uuid) {
        const revision = taskRevision
        if (!taskManager || !taskManager.tasks || !uuid || String(uuid).length === 0) {
            return ({})
        }
        return taskManager.tasks.taskForModel(uuid, taskType, false)
    }

    function hasTask(task) {
        return task && task.task_id !== undefined && task.task_id >= 0
    }

    function canStartModelTask(uuid) {
        if (!taskActionsEnabled || !taskManager || !uuid || String(uuid).length === 0) {
            return false
        }

        const task = taskForModel(uuid)
        return hasTask(task) ? task.can_start === true : true
    }

    function canStopModelTask(uuid) {
        if (!taskActionsEnabled || !taskManager || !uuid || String(uuid).length === 0) {
            return false
        }

        const task = taskForModel(uuid)
        return hasTask(task) ? task.can_stop === true : false
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
            text: "添加到任务"
            iconSource: QuiFontIcon.TaskView
            visible: modelView.taskActionsEnabled
            enabled: modelView.taskManager && modelView.currentModelUuid.length > 0
            onClicked: modelView.addCurrentModelTask()
        }
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
                    modelView.requestEnsureCurrentModel()
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
                    modelFormDialog.frameworkModel = modelManager.supportedFrameworks()
                    const firstFramework = modelFormDialog.frameworkModel.length > 0 ? modelFormDialog.frameworkModel[0] : ""
                    modelFormDialog.architectureModel = firstFramework.length > 0
                                                        ? modelManager.supportedModelArchitectures(firstFramework)
                                                        : []
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
                        modelView.selectModel(currentIndex)
                    } else if (count <= 0) {
                        modelView.resetCurrentModelState()
                    }
                }

                delegate: ModelDelegate {
                    width: view.width
                    modelId: model.model_id
                    modelUuid: model.uuid
                    modelName: model.name
                    frameworkName: model.framework_name
                    modelArchitecture: model.model_architecture
                    trainingResult: model.training_result
                    testResult: model.test_result
                    selected: ListView.isCurrentItem
                    showTaskActions: modelView.taskActionsEnabled
                    taskActionsEnabled: modelView.taskManager !== null && model.uuid
                    startTaskEnabled: modelView.canStartModelTask(model.uuid)
                    stopTaskEnabled: modelView.canStopModelTask(model.uuid)
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
        onFrameworkChanged: function (frameworkName) {
            if (modelManager) {
                modelFormDialog.architectureModel = modelManager.supportedModelArchitectures(frameworkName)
            }
        }
        onSubmitted: function (modelName, frameworkName, modelArchitecture) {
            if (modelManager) {
                modelManager.addModel(modelName, frameworkName, modelArchitecture)
            }
        }
    }
}
