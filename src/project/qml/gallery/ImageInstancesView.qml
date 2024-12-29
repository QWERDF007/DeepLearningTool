import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.project

Item {
    id: instancesView
    width: 800
    height: 600

    property int cellWidth: 330
    property int cellHeight: 250
    property int spacing: 10

    GridView {
        id: view
        clip: true
        anchors.fill: parent
        cellWidth: instancesView.cellWidth + instancesView.spacing
        cellHeight: instancesView.cellHeight + instancesView.spacing
        ScrollBar.vertical: DltScrollBar {}
        model:  ProjectManager.currentProject ? ProjectManager.currentProject.imageInstances : null
        delegate: ImageInstanceDelegate {
            width: instancesView.cellWidth
            height: instancesView.cellHeight
            image.source: "file:///" + model.path
        }
    }
}
