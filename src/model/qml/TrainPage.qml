import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.model
import dltool.project

Rectangle {
    id: labelPage
    width: 1080
    height: 1920
    color: DltColor.Background

    property ModelManager modelManager: ProjectManager.currentProject ? ProjectManager.currentProject.modelManager : null

    DltSplitView {
        anchors.fill: parent
        anchors.margins: 5

        ModelView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.preferredWidth: 300
            SplitView.maximumWidth: parent.width / 2
            color: DltColor.Primary
            headerTitle: "模型训练:"
            addEnable: true
            modelManager: labelPage.modelManager
        }

        Item {
            SplitView.fillHeight: true
            SplitView.fillWidth: true
        }
    }
}
