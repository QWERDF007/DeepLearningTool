import QtQuick
import QtQuick.Controls

import dltool.project

Item {
    id: labelView

    property Project project: ProjectManager.currentProject

    ListModel {
        id: labelModel
        ListElement {
            x: 0
            y: 0
            width: 50
            height: 50
            color: "red"
        }
        ListElement {
            x: 100
            y: 0
            width: 50
            height: 50
            color: "blue"
        }
        ListElement {
            x: 100
            y: 100
            width: 50
            height: 50
            color: "green"
        }
    }

    LabelImage {
        id: labelImage
        anchors.fill: parent
        curImagePath: project ? project.imageInstances.curImagePath : ""

        Repeater {
            model: labelImage.image.status === Image.Ready ? labelModel : null
            Rectangle {
                x: model.x
                y: model.y
                width: model.width
                height: model.height
                color: model.color
            }
        }
    }

    MouseArea {
        anchors.fill: parent

        onPressed: function(event) {
            
        }
    }
}