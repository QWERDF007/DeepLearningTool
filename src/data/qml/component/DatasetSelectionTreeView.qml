import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.data
import dltool.ui
import quickui

Rectangle {
    id: control

    property string roleTitle: ""
    property DataSelectionTreeModel selectionModel: null
    property string title: roleTitle.length > 0 ? roleTitle : qsTr("数据集/类别")
    property int iconSource: -1
    property bool showColor: true
    readonly property int selectedCount: selectionModel ? selectionModel.selectedCount : 0

    signal selectionEdited(var ids)

    radius: 4
    color: QuiColor.Primary
    border.color: QuiColor.Background
    clip: true

    function selectedIds() {
        return selectionModel ? selectionModel.selectedIds() : []
    }

    function setSelectedIds(ids) {
        if (selectionModel) {
            selectionModel.setSelectedIds(ids ? ids : [])
        }
    }

    function clearSelection() {
        if (selectionModel) {
            selectionModel.clearSelection()
        }
    }

    function selectAll() {
        if (selectionModel) {
            selectionModel.selectAll()
        }
    }

    Connections {
        target: control.selectionModel
        function onSelectionChanged() {
            control.selectionEdited(control.selectedIds())
        }
    }

    ColumnLayout {
        id: content

        anchors.fill: parent
        anchors.margins: 8
        spacing: 5

        RowLayout {
            Layout.fillWidth: true
            spacing: 5

            QuiText {
                Layout.fillWidth: true
                text: control.title
                color: QuiColor.FontPrimary
                elide: Text.ElideRight
            }

            QuiText {
                text: String(control.selectedCount)
                color: QuiColor.FontDark
            }

            QuiTextIconButton {
                iconSource: QuiFontIcon.CheckList
                iconSize: 14
                text: qsTr("全选")
                enabled: control.selectionModel !== null && control.selectionModel.rowCount() > 0
                onClicked: control.selectAll()
            }

            QuiTextIconButton {
                iconSource: QuiFontIcon.Clear
                iconSize: 14
                text: qsTr("清空")
                enabled: control.selectedCount > 0
                onClicked: control.clearSelection()
            }
        }

        QuiTreeView {
            id: tree

            Layout.fillWidth: true
            Layout.fillHeight: true
            model: control.selectionModel
            displayRole: "name"
            colorRole: control.showColor ? "color" : ""
            iconSource: control.iconSource
            checkable: true
            checkedResolver: function(row, rowData) {
                return rowData && rowData.selected === true
            }
            partiallyCheckedResolver: function(row, rowData) {
                return rowData && rowData.partially_selected === true
            }
            onItemClicked: function(row, rowData) {
                if (control.selectionModel && rowData) {
                    control.selectionModel.toggleNode(rowData.dataset_id, rowData.label_class_id)
                }
            }
            onItemCheckToggled: function(row, rowData, checked) {
                if (control.selectionModel && rowData) {
                    control.selectionModel.setNodeSelected(rowData.dataset_id, rowData.label_class_id, checked)
                }
            }
        }
    }
}
