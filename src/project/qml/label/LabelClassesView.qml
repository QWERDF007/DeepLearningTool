import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import dltool.project
import dltool.ui

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
            spacing: 5
            Layout.fillHeight: true
            Layout.fillWidth: true
            model: labelClassesView.project ? labelClassesView.project.labelClasses : null
            delegate:  Rectangle {
                width: parent.width
                height: 32
                color: Qt.lighter(DltColor.Primary, 1.2)
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 5
                    anchors.rightMargin: 5
                    Rectangle {
                        Layout.preferredWidth: 32
                        Layout.fillHeight: true
                        radius: 3
                        color: model.color
                    }
                    DltText {
                        text: model.name
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
