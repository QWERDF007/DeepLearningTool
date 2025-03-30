import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import dltool.project

Rectangle {
    id: labelClassesView
    clip: true
    width: 200
    height: 200
    color: DltColor.Primary
    property Project project: ProjectManager.currentProject

    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        LabelClassesHeader {
            Layout.fillWidth: true
            height: 32
            project: labelClassesView.project
        }
        ListView {
            id: view
            clip: true
            Layout.fillHeight: true
            Layout.fillWidth: true
        }
    }
}
