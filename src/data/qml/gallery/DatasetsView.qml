import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle {
    id: datasetsView
    width: 200
    height: 200
    color: DltColor.Primary
    property Project project: ProjectManager.currentProject

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        DatasetHeader {
            Layout.fillWidth: true
            height: 32
            project: datasetsView.project
        }

        ListView {
            id: view
            clip: true
            Layout.fillHeight: true
            Layout.fillWidth: true
            model: datasetsView.project ? datasetsView.project.datasets : null
            ScrollBar.vertical: DltScrollBar {}
            delegate: DatasetDelegate {
                height: 32
                width: view.width
                name: model.name
                stats: model.stats
                dataset_id: model.dataset_id
            }
        }
    }
}
