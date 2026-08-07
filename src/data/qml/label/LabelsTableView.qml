import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.feature
import dltool.settings
import quickui

Rectangle {
    id: control
    color: QuiColor.Primary
    width: 200
    height: 200

    property DataManager dataManager
    property FeatureManager featureManager
    property ImageLabelsTableModel imageLabelsTable: dataManager ? dataManager.imageLabelsTable : null
    property ItemSelectionModel selection: imageLabelsTable ? imageLabelsTable.selection : null
    property var roiCluster: featureManager ? featureManager.roiCluster : null
    property bool roiClusterEnabled: true
    readonly property bool tableActive: dataManager !== null && imageLabelsTable !== null

    property real rowHeight: 24
    property real classColumnWidth: 80
    property real minimumColumnWidth: 60

    function sameColumnSchema(left, right) {
        if (!left || left.length !== right.length)
            return false
        for (let i = 0; i < right.length; ++i) {
            if (String(left[i].dataIndex) !== String(right[i].dataIndex))
                return false
        }
        return true
    }

    function rebuildTable() {
        let columns = []
        let rows = []
        if (control.tableActive) {
            let columnCount = imageLabelsTable.columnCount()
            let rowCount = imageLabelsTable.rowCount()
            for (let column = 0; column < columnCount; ++column) {
                columns.push({
                    title: imageLabelsTable.headerData(column, Qt.Horizontal, Qt.DisplayRole),
                    dataIndex: "column_" + column,
                    width: column === 0 ? control.classColumnWidth : control.minimumColumnWidth,
                    minimumWidth: control.minimumColumnWidth,
                    stretch: column !== 0,
                    resizable: true,
                    frozen: column === 0
                })
            }
            for (let row = 0; row < rowCount; ++row) {
                let rowData = {}
                for (let column = 0; column < columnCount; ++column) {
                    let index = imageLabelsTable.index(row, column)
                    let role = column === 0 ? ImageLabelsTableModel.ClassDataRole
                                            : ImageLabelsTableModel.DataRole
                    rowData["column_" + column] = tableView.customItem(
                        column === 0 ? com_class_cell : com_data_cell,
                        {
                            mdata: imageLabelsTable.data(index, role),
                            selected: imageLabelsTable.data(index, ImageLabelsTableModel.SelectedRole) || false
                        })
                }
                rows.push(rowData)
            }
        }
        if (!sameColumnSchema(tableView.columnSource, columns))
            tableView.columnSource = columns
        tableView.dataSource = rows
    }

    function preferredColumnWidth(column) {
        if (column === 0) {
            return classColumnWidth
        }
        if (!tableActive) {
            return minimumColumnWidth
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
        function onImageLabelListSelectAll() {
            if (imageLabelsTable) {
                imageLabelsTable.selectAll()
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
            control.rebuildTable()
            // Select and scroll to the last inserted row
            Qt.callLater(function() { selectAndScrollToRow(last) })
        }
        function onRowsRemoved(parent, first, last) {
            control.rebuildTable()
        }
        function onModelReset() {
            control.rebuildTable()
        }
        function onDataChanged(topLeft, bottomRight, roles) {
            control.rebuildTable()
        }
    }

    onImageLabelsTableChanged: rebuildTable()

    Component {
        id: com_class_cell
        ClassColumnDelegate {
            implicitWidth: tableView.columnWidth(column)
            implicitHeight: control.rowHeight
            mdata: options.mdata
            selected: options.selected
        }
    }

    Component {
        id: com_data_cell
        DataColumnDelegate {
            implicitWidth: tableView.columnWidth(column)
            implicitHeight: control.rowHeight
            mdata: options.mdata
            selected: options.selected
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
        QuiMenuItem {
            text: "标注聚类"
            enabled: dataManager && roiCluster && selection && selection.hasSelection
                     && !roiCluster.running && roiClusterEnabled && roiCluster.enabled
            iconSource: QuiFontIcon.GridView
            onClicked: startRoiClusterForSelectedLabels()
        }
    }

    RoiClusterDialog {
        id: roiClusterDialog
        dataManager: control.dataManager
        featureManager: control.featureManager
        roiClusterEnabled: control.roiClusterEnabled
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
        anchors.leftMargin: 0
        anchors.rightMargin: 0
        anchors.topMargin: 5
        anchors.bottomMargin: 5

        // spacing: 5
        QuiText {
            Layout.leftMargin: 5
            Layout.rightMargin: 5
            text: "标签实例:"
            font: QuiFont.Subtitle
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            QuiTableView {
                id: tableView
                anchors.fill: parent
                headerHeight: 32
                rowHeight: control.rowHeight
                headerColor: QuiColor.Background
                headerTextColor: "white"
                columnSpacing: 2
                fitColumnsToWidth: true
                minimumColumnWidth: control.minimumColumnWidth
                columnSource: []
                dataSource: []
                rowSelectionEnabled: false

                rowHeightProvider: function(row) {
                    return control.rowHeight
                }

            }

            MouseArea {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: tableView.headerHeight
                anchors.bottom: parent.bottom
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                enabled: control.tableActive
                z: 20
                onClicked: function(mouse) {
                    control.forceActiveFocus()
                    if (!control.tableActive || selection === null) {
                        return
                    }
                    let pos = mapToItem(tableView.view, mouse.x, mouse.y)
                    if (pos.x < 0 || pos.y < 0 || pos.x > tableView.view.width || pos.y > tableView.view.height) {
                        return
                    }
                    let row = Math.floor((pos.y + tableView.view.contentY) / control.rowHeight)
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
        }
    }

    Keys.enabled: control.visible && control.tableActive
    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_A) && (event.modifiers & Qt.ControlModifier)) {
            control.selectAll()
        } else if (event.key === Qt.Key_Delete && selection && selection.hasSelection) {
            deleteConfirmDialog.open()
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
        if (!control.tableActive || !selection || !imageLabelsTable) {
            return
        }
        
        // Select the row using ClearAndSelect flag
        let tmpIndex = imageLabelsTable.index(row, 0)
        control.select(tmpIndex, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
        
        // Calculate the row position and scroll to make it visible
        let rowY = row * control.rowHeight
        let viewportHeight = tableView.view.height
        
        // Check if the row is not visible in the current viewport
        if (rowY < tableView.view.contentY || rowY + control.rowHeight > tableView.view.contentY + viewportHeight) {
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

    function startRoiClusterForSelectedLabels() {
        if (!dataManager || !roiCluster || !imageLabelsTable || !selection
                || !selection.hasSelection || !roiClusterEnabled || roiCluster.running
                || !roiCluster.enabled) {
            return
        }

        let labelIds = imageLabelsTable.getSelectedLabelIds()
        if (labelIds.length > 0) {
            roiClusterDialog.openForLabels(labelIds)
        }
    }

    function refreshSettings() {
        roiClusterEnabled = GlobalSettings.valueForField(SettingsAccessor.RoiCluster, RoiClusterField.Enabled, true)
    }

    Component.onCompleted: {
        refreshSettings()
        rebuildTable()
    }

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            control.refreshSettings()
        }
    }
}
