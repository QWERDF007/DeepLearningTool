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
    property ItemSelectionModel selection: project ? project.labelClasses.selection : null
    
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
            boundsBehavior: Flickable.StopAtBounds
            Layout.fillHeight: true
            Layout.fillWidth: true
            model: labelClassesView.project ? labelClassesView.project.labelClasses : null
            delegate:  LabelClassDelegate {
                width: view.width
                height: 32
                color: model.selected ? DltColor.Highlight : Qt.lighter(DltColor.Primary, 1.2)
                className: model.name
                classColor: model.color
                onClicked: function() {
                    console.log("clicked")
                    selection.select(view.model.index(index, 0), ItemSelectionModel.ClearAndSelect)
                }
                onEditClicked: function() {
                    console.log("editClicked")
                }
                onDeleteClicked: function () {
                    console.log("deleteClicked", model.label_class_id)
                    project.deleteLabelClass(model.label_class_id)
                }
            }
        }
    }
}
