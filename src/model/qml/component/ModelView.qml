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
    property var editingModelId: -1
    property string currentModelUuid: ""
    property string currentModelName: ""
    property string currentFrameworkName: ""
    property string currentModelArchitecture: ""
    property string currentModelCtime: ""
    property string currentModelMtime: ""
    property bool componentCompleted: false
    property var taskManager: null
    property ModelTestTaskManager testTaskManager: null
    property ModelTaskController taskController: null
    property int taskType: ModelTaskTypes.Unknown
    // Test task state is scoped by the selected test-task UUID.  Training
    // keeps the single train scope and does not share the test-task cache.
    property string taskScopeUuid: ""
    property bool taskActionsEnabled: false
    property int taskRevision: taskManager ? taskManager.revision : 0

    function taskExtraData(modelData) {
        if (!modelData || !modelData.extra_data)
            return ({})

        if (taskType === ModelTaskTypes.Train)
            return modelData.extra_data.train || ({})
        if (taskType === ModelTaskTypes.Test && modelData.uuid === currentModelUuid
                && taskScopeUuid.length > 0) {
            const testTasks = modelData.extra_data.test_tasks || ({})
            return testTasks[taskScopeUuid] || ({})
        }
        return ({})
    }

    function resetCurrentModelState() {
        currentModelId = -1
        currentModelUuid = ""
        currentModelName = ""
        currentFrameworkName = ""
        currentModelArchitecture = ""
        currentModelCtime = ""
        currentModelMtime = ""
        if (view.currentIndex !== -1) {
            view.currentIndex = -1
        }
    }

    function selectModel(row) {
        if (!modelManager || row < 0 || row >= view.count) {
            resetCurrentModelState()
            return
        }

        const modelData = modelManager.userVisibleModelAt(row)
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
        currentModelCtime = modelData.ctime || ""
        currentModelMtime = modelData.mtime || ""
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
        if (taskType === ModelTaskTypes.Test) {
            // 运行前提交当前数据集选择与参数落库，本次运行使用界面上的最新选择。
            if (testTaskManager && !testTaskManager.commitCurrentDatasetSelection()) {
                return
            }
            taskController.startModelTestTask(currentModelUuid, taskScopeUuid)
        } else {
            taskController.startModelTask(currentModelUuid, taskType)
        }
    }

    function stopCurrentModelTask() {
        if (!canStopModelTask(currentModelUuid)) {
            return
        }
        if (taskType === ModelTaskTypes.Test) {
            taskController.stopModelTestTask(currentModelUuid, taskScopeUuid)
        } else {
            taskController.stopModelTask(currentModelUuid, taskType)
        }
    }

    function addCurrentModelTask() {
        if (!taskActionsEnabled || !taskController || currentModelUuid.length === 0) {
            return
        }
        taskController.addModelTask(currentModelUuid, taskType)
    }

    function taskIdForModel(uuid) {
        const revision = taskRevision
        if (!taskManager || !uuid || String(uuid).length === 0) {
            return -1
        }
        if (taskType === ModelTaskTypes.Test) {
            if (taskScopeUuid.length === 0)
                return -1
            return taskManager.findModelTask(uuid, taskType, taskScopeUuid, false)
        }
        return taskManager.findModelTask(uuid, taskType, false)
    }

    function canStartModelTask(uuid) {
        if (!taskActionsEnabled || !taskController || !taskManager || !uuid || String(uuid).length === 0
                || (taskType === ModelTaskTypes.Test && taskScopeUuid.length === 0)) {
            return false
        }

        const taskId = taskIdForModel(uuid)
        return taskId < 0 || taskManager.canStartTask(taskId)
    }

    function canStopModelTask(uuid) {
        if (!taskActionsEnabled || !taskController || !taskManager || !uuid || String(uuid).length === 0
                || (taskType === ModelTaskTypes.Test && taskScopeUuid.length === 0)) {
            return false
        }

        const taskId = taskIdForModel(uuid)
        return taskId >= 0 && taskManager.canStopTask(taskId)
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
            visible: modelView.taskActionsEnabled && modelView.taskType !== ModelTaskTypes.Test
            enabled: modelView.taskController && modelView.currentModelUuid.length > 0
            onClicked: modelView.addCurrentModelTask()
        }
        QuiMenuItem {
            text: "重命名"
            iconSource: QuiFontIcon.Rename
            onClicked: {
                if (modelView.currentModelId < 0)
                    return
                modelView.editingModelId = modelView.currentModelId
                modelFormDialog.title = "重命名"
                modelFormDialog.metadataEditable = false
                modelFormDialog.frameworkModel = [modelView.currentFrameworkName]
                modelFormDialog.architectureModel = [modelView.currentModelArchitecture]
                modelFormDialog.openForm(modelView.currentModelName, modelView.currentFrameworkName,
                                         modelView.currentModelArchitecture)
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
                    modelView.editingModelId = -1
                    modelFormDialog.title = "添加模型"
                    modelFormDialog.metadataEditable = true
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
                model: modelManager ? modelManager.userVisibleModel : null
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
                    createdTime: model.ctime
                    modifiedTime: model.mtime
                    extraData: modelView.taskExtraData(model)
                    selected: ListView.isCurrentItem
                    showTaskActions: modelView.taskActionsEnabled
                    taskActionsEnabled: modelView.taskController !== null && model.uuid
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
        nameValidator: function (modelName) {
            if (!modelManager) {
                return "模型管理器不可用"
            }
            return modelManager.validateModelName(modelName)
        }
        onFrameworkChanged: function (frameworkName) {
            if (modelManager) {
                modelFormDialog.architectureModel = modelManager.supportedModelArchitectures(frameworkName)
            }
        }
        onSubmitted: function (modelName, frameworkName, modelArchitecture) {
            if (!modelManager) {
                return
            }
            if (modelView.editingModelId >= 0) {
                if (modelManager.renameModel(modelView.editingModelId, modelName)) {
                    modelView.currentModelName = modelName
                }
            } else {
                modelManager.addModel(modelName, frameworkName, modelArchitecture)
            }
        }
    }
}
