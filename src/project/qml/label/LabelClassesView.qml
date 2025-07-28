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
    property LabelClassesModel labelClasses: project ? project.labelClasses : null
    property ItemSelectionModel selection: project ? project.labelClasses.selection : null

    LabelClassEditor {
        id: editor
        isCreate: false
        maxOrdinalIndex: view.count
        onLabelClassChanged: function (classId, className, classColor, classShortcut, ordinalIndex) {
            if (labelClasses) {
                editor.msg = labelClasses.isValid(classId, className, classShortcut, ordinalIndex)
            }
        }
        onLabelClassChangedAccepted: function (classId, className, classColor, classShortcut, ordinalIndex) {
            if (project) {
                project.updateLabelClass(classId, className, classColor, classShortcut, ordinalIndex)
            }
        }
    }
    
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
            ScrollBar.vertical: DltScrollBar {}
            Layout.fillHeight: true
            Layout.fillWidth: true
            model: labelClassesView.labelClasses
            delegate:  LabelClassDelegate {
                width: view.width - 8
                height: 32
                color: model.selected ? DltColor.Highlight : Qt.lighter(DltColor.Primary, 1.2)
                className: model.name
                classColor: model.color
                classShortcut: model.shortcut
                onClicked: function() {
                    let tmpIndex = view.model.index(index, 0)
                    selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
                    selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                }
                onEditClicked: function() {
                    let pos = mapToItem(null, 0, 0)
                    editor.x = pos.x + width
                    editor.y = pos.y + 10
                    editor.classId = model.label_class_id
                    editor.className = model.name
                    editor.classColor = model.color
                    editor.classShortcut = model.shortcut
                    editor.ordinalIndex = model.ordinal_index
                    editor.open()
                }
                onDeleteClicked: function () {
                    project.deleteLabelClass(model.label_class_id)
                }
            }
        }
    }
}
