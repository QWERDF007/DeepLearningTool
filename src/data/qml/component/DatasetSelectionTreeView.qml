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
    property string emptyText: qsTr("暂无数据")
    property int iconSource: -1
    property bool showColor: true
    property int treeHeight: 176
    readonly property int selectedCount: selectionModel ? selectionModel.selectedCount : 0

    signal selectionEdited(var ids)

    radius: 4
    color: QuiColor.Primary
    border.color: QuiColor.Background
    clip: true
    implicitHeight: content.implicitHeight + 16

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
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

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
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                iconSource: QuiFontIcon.CheckList
                iconSize: 14
                text: qsTr("全选")
                enabled: control.selectionModel !== null && control.selectionModel.rowCount() > 0
                onClicked: control.selectAll()
            }

            QuiTextIconButton {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                iconSource: QuiFontIcon.Clear
                iconSize: 14
                text: qsTr("清空")
                enabled: control.selectedCount > 0
                onClicked: control.clearSelection()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: control.treeHeight
            color: QuiColor.Transparent
            border.color: QuiColor.Background

            QuiTreeView {
                id: tree

                anchors.fill: parent
                anchors.margins: 2
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

            QuiText {
                anchors.centerIn: parent
                width: Math.max(0, parent.width - 24)
                visible: !control.selectionModel || control.selectionModel.rowCount() === 0
                text: control.emptyText
                color: QuiColor.FontDark
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }
}
