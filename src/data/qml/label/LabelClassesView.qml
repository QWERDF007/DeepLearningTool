import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import dltool.data
import dltool.ui

Rectangle {
    id: labelClassesView
    clip: true
    width: 200
    height: 200
    color: DltColor.Primary
    property DataManager dataManager
    property LabelClassesModel labelClasses: dataManager ? dataManager.labelClasses : null
    property ItemSelectionModel selection: dataManager ? dataManager.labelClasses.selection : null
    property int _label_class_id: -1

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
            if (dataManager) {
                dataManager.updateLabelClass(classId, className, classColor, classShortcut, ordinalIndex)
            }
        }
    }

    DltContentDialog {
        id: deleteConfirmDialog
        title: "删除标签类别"
        message: "确定删除选中的标签类别吗?"
        usePositiveButton: true
        useNegativeButton: true
        onPositiveClicked: function () {
            if (dataManager) {
                dataManager.deleteLabelClass(_label_class_id)
            }
        }
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        LabelClassesHeader {
            Layout.fillWidth: true
            height: 32
            dataManager: labelClassesView.dataManager
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
                    let tmpIndex = labelClasses.index(index, 0)
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
                    _label_class_id = model.label_class_id
                    deleteConfirmDialog.open()
                }
            }
        }
    }
}
