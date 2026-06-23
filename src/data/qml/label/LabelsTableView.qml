import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.qmlmodels

import dltool.ui
import dltool.data
import quickui

Rectangle {
    id: control
    color: QuiColor.Primary
    width: 200
    height: 200

    property DataManager dataManager
    property ImageLabelsTableModel imageLabelsTable: dataManager ? dataManager.imageLabelsTable : null
    property ItemSelectionModel selection: imageLabelsTable ? imageLabelsTable.selection : null

    property real rowHeight: 24
    property real classColumnWidth: 120
    property real minimumColumnWidth: 80

    function preferredColumnWidth(column) {
        if (column === 0) {
            return classColumnWidth
        }
        let dataColumns = Math.max(1, tableView.columns - 1)
        let availableWidth = tableView.view.width - tableView.columnWidth(0) - 8
        return Math.max(minimumColumnWidth, availableWidth / dataColumns)
    }

    Connections {
        target: SignalHelper
        function onImageLabelListSelectionChanged(index, command) {
            if (selection) {
                selection.select(index, command)
            }
        }
        function onSelectLabel(label_id) {
            if (imageLabelsTable) {
                control.selectLabelById(label_id)
            }
        }
    }

    Connections {
        target: imageLabelsTable
        function onRowsInserted(parent, first, last) {
            // Select and scroll to the last inserted row
            selectAndScrollToRow(last)
        }
    }

    QuiMenu {
        id: tableViewMenu
        width: 200
        QuiMenuItem {
            text: "删除选中标签"
            enabled : selection ? selection.hasSelection : false
            iconSource: QuiFontIcon.Delete
            onClicked: {
                deleteConfirmDialog.open()
            }
        }
    }

    QuiContentDialog {
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
        anchors.leftMargin: 5
        anchors.rightMargin: 0
        anchors.topMargin: 5
        anchors.bottomMargin: 5

        // spacing: 5
        QuiText {
            text: "标签实例:"
            font: QuiFont.Subtitle
        }

        QuiTableView {
            id: tableView
            Layout.fillWidth: true
            Layout.fillHeight: true
            headerHeight: 32
            rowHeight: control.rowHeight
            headerColor: QuiColor.Background
            headerTextColor: "white"
            columnSpacing: 0
            minimumColumnWidth: control.minimumColumnWidth
            columnSource: [
                {
                    width: control.classColumnWidth,
                    minimumWidth: control.minimumColumnWidth,
                    frozen: true
                }
            ]
            model: imageLabelsTable

            columnWidthProvider: function(column) {
                return control.preferredColumnWidth(column)
            }

            rowHeightProvider: function(row) {
                return rowHeight
            }

            delegate: DelegateChooser {

                DelegateChoice {
                    column: 0
                    ClassColumnDelegate {
                        implicitWidth: tableView.columnWidth(column)
                        implicitHeight: rowHeight
                        mdata: model.class_data
                        selected: model.selected ?? false
                    }
                }

                DelegateChoice {
                    DataColumnDelegate {
                        implicitWidth: tableView.columnWidth(column)
                        implicitHeight: rowHeight
                        mdata: model.data
                        selected: model.selected ?? false
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
        parent: tableView.bodyOverlay
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function(mouse) {
            control.forceActiveFocus()
            if (selection === null) {
                return
            }
            let pos = mapToItem(tableView.view, mouse.x, mouse.y)
            if (pos.x < 0 || pos.y < 0 || pos.x > tableView.view.width || pos.y > tableView.view.height) {
                return
            }
            let row = Math.floor((pos.y + tableView.view.contentY) / rowHeight)
            if (row < 0) {
                return
            }
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

    function selectAndScrollToRow(row) {
        if (!selection || !imageLabelsTable) {
            return
        }
        
        // Select the row using ClearAndSelect flag
        let tmpIndex = imageLabelsTable.index(row, 0)
        control.select(tmpIndex, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
        
        // Calculate the row position and scroll to make it visible
        let rowY = row * rowHeight
        let viewportHeight = tableView.view.height
        
        // Check if the row is not visible in the current viewport
        if (rowY < tableView.view.contentY || rowY + rowHeight > tableView.view.contentY + viewportHeight) {
            // Scroll to position the row in the middle of the viewport
            tableView.view.contentY = Math.max(0, rowY - viewportHeight / 2)
        }
    }

    function selectLabelById(label_id) {
        if (!imageLabelsTable || !selection) {
            return
        }
        
        let row = imageLabelsTable.findRowByLabelId(label_id)
        if (row >= 0) {
            selectAndScrollToRow(row)
        } else {
            console.warn("Label ID not found in current image:", label_id)
        }
    }
}
