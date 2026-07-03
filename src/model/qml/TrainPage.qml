import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import dltool.ui
import dltool.data
import dltool.model
import dltool.project
import "train"
import quickui

Rectangle {
    id: labelPage
    width: 1080
    height: 1920
    color: QuiColor.Background

    property ModelManager modelManager: ProjectManager.currentProject ? ProjectManager.currentProject.modelManager : null
    property DataManager dataManager: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager : null

    QuiSplitView {
        anchors.fill: parent
        anchors.margins: 5

        ModelView { // 左侧模型列表
            id: modelView
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.preferredWidth: 300
            SplitView.maximumWidth: parent.width / 2
            color: QuiColor.Primary
            headerTitle: "模型训练:"
            addEnable: true
            modelManager: labelPage.modelManager
            taskManager: ProjectManager.currentProject ? ProjectManager.currentProject.taskManager : null
            taskType: qsTr("训练")
            taskActionsEnabled: true
        }

        TrainPanel {
            SplitView.fillHeight: true
            SplitView.fillWidth: true
            dataManager: labelPage.dataManager
            modelManager: labelPage.modelManager
            currentModelId: modelView.currentModelId
            currentModelUuid: modelView.currentModelUuid
            currentModelName: modelView.currentModelName
            currentNetworkStructure: modelView.currentNetworkStructure
        }
    }
}
