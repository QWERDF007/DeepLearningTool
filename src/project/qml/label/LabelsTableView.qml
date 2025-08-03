import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.qmlmodels

import dltool.ui
import dltool.project

Rectangle {
    id: control
    color: DltColor.Primary
    width: 200
    height: 200

    property Project project: ProjectManager.currentProject
    property DataManager dataManager: project ? project.dataManager : null
    property ImageLabelsTableModel imageLabelsTable: dataManager ? dataManager.imageLabelsTable : null
    property ItemSelectionModel selection: imageLabelsTable ? imageLabelsTable.selection : null

    property real rowHeight: 24
    property real colWidth: horizontalHeader.width / horizontalHeader.columns

    Connections {
        target: SignalHelper
        function onImageLabelListSelectionChanged(index, command) {
            if (selection) {
                selection.select(index, command)
            }
        }
    }

    DltMenu {
        id: tableViewMenu
        width: 200
        DltMenuItem {
            text: "删除选中标签"
            enabled : selection ? selection.hasSelection : false
            iconSource: DltFontIcon.Delete
            onClicked: {
                deleteConfirmDialog.open()
            }
        }
    }

    DltConfirmDialog {
        id: deleteConfirmDialog
        title: "删除标签实例"
        message: "确定删除选中的标签实例吗?"
        onPositiveClicked: function () {
            if (dataManager) {
                let label_ids = imageLabelsTable.getSelectedLabelIds()
                dataManager.deleteLabels(label_ids)
            }
        }
    }


    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        // spacing: 5
        DltText {
            text: "标签实例:"
            font: DltFont.Subtitle
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true

            LabelsTableHeader {
                id: horizontalHeader
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                syncView: tableView
            }

            TableView {
                id: tableView
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true
                boundsBehavior: Flickable.StopAtBounds

                model: imageLabelsTable

                delegate: DelegateChooser {

                    DelegateChoice{
                        column: 0
                        ClassColumnDelegate {
                            implicitWidth: colWidth
                            implicitHeight: rowHeight
                            mdata: model.data
                            selected: model.selected
                        }
                    }

                    DelegateChoice {
                        DataColumnDelegate {
                            implicitWidth: colWidth
                            implicitHeight: rowHeight
                            mdata: model.data
                            selected: model.selected
                        }
                    }
                }
            }
        }
    }

    Keys.enabled: control.visible
    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_A) && (event.modifiers & Qt.ControlModifier)) {
            control.selectAll()
        } else if (event.key === Qt.Key_Delete && selection && selection.hasSelection) {
            deleteConfirmDialog.open()
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function(mouse) {
            control.forceActiveFocus()
            if (selection === null) {
                return
            }
            let pos = mapToItem(tableView, mouse.x, mouse.y)
            let row = Math.floor(pos.y / rowHeight)
            if (row >= imageLabelsTable.rowCount()) {
                control.clearSelection()
                return
            }
            let tmpIndex = imageLabelsTable.index(row, 0)
            if (imageLabelsTable.lastIndex === -1) {
                imageLabelsTable.lastIndex = row
            }
            if (mouse.button === Qt.LeftButton || (mouse.button === Qt.RightButton && !selection.isSelected(tmpIndex))) {
                if (mouse.modifiers & Qt.ShiftModifier) { // shift 多选
                    control.shiftSelect(row, imageLabelsTable.lastIndex, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                } else if (mouse.modifiers & Qt.ControlModifier) { // ctrl 多选
                    control.select(tmpIndex, ItemSelectionModel.Select | ItemSelectionModel.Rows)
                } else { // 单选
                    control.select(tmpIndex, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                    imageLabelsTable.lastIndex = row
                }
            }
            if (mouse.button === Qt.RightButton) {
                tableViewMenu.popup()
            }
        }
    }

    function select(index, command) {
        if (selection) {
            selection.select(index, command)
            SignalHelper.imageLabelTableSelectionChanged(index, command)
        }
    }

    function shiftSelect(currentIndex, lastIndex, command) {
        if (selection) {
            imageLabelsTable.shiftSelect(currentIndex, lastIndex, command)
            SignalHelper.imageLabelTableShiftSelect(currentIndex, lastIndex, command)
        }
    }

    function selectAll() {
        if (selection) {
            imageLabelsTable.selectAll()
            SignalHelper.imageLabelTableSelectAll()
        }
    }

    function clearSelection() {
        if (selection) {
            selection.clear()
            SignalHelper.imageLabelTableSelectionClear()
        }
    }
}
