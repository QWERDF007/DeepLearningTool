import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.model
import dltool.project
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

        TestPanel {
            modelManager: labelPage.modelManager
            dataManager: labelPage.dataManager
            currentModelUuid: modelView.currentModelUuid
        }
    }
}
