import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.feature
import quickui

Rectangle {
    id: datasetsView
    width: 200
    height: 200
    color: QuiColor.Primary
    property DataManager dataManager
    property FeatureManager featureManager
    property DatasetsModel datasets: dataManager ? dataManager.datasets : null
    property ItemSelectionModel selection: datasets ? datasets.selection : null
    property int selectedCount: selection ? selection.selectedIndexes.length : 0
    property var curItem: null
    property var activeDatasetIds: []
    property bool datasetFilterEnabled: false
    property bool datasetFilterInverted: false

    function selectedDatasetIds() {
        return datasets ? datasets.getSelectedDatasetIds() : []
    }

    function hasCurItem() {
        return curItem !== null && curItem !== undefined
    }

    function syncDatasetFilterState() {
        if (!dataManager || !dataManager.globalFilter) {
            activeDatasetIds = []
            datasetFilterEnabled = false
            datasetFilterInverted = false
            return
        }

        datasetFilterEnabled = dataManager.globalFilter.isFilterEnabled(GlobalFilter.FilterType.Dataset)
        datasetFilterInverted = dataManager.globalFilter.isFilterInverted(GlobalFilter.FilterType.Dataset)
        let ids = dataManager.globalFilter.getActiveIds(GlobalFilter.FilterType.Dataset)
        activeDatasetIds = ids ? ids : []
    }

    function containsId(ids, id) {
        for (let i = 0; i < ids.length; ++i) {
            if (ids[i] === id) {
                return true
            }
        }
        return false
    }

    function visibleDatasetIds() {
        let allIds = dataManager ? dataManager.getAllDatasetIds() : []
        if (!datasetFilterEnabled) {
            return allIds
        }

        let ids = []
        for (let i = 0; i < allIds.length; ++i) {
            let inCriteria = containsId(activeDatasetIds, allIds[i])
            if (datasetFilterInverted ? !inCriteria : inCriteria) {
                ids.push(allIds[i])
            }
        }
        return ids
    }

    function isDatasetFiltered(datasetId) {
        return datasetFilterEnabled && !containsId(visibleDatasetIds(), datasetId)
    }

    function toggleDatasetFilter(datasetId) {
        if (!dataManager || !dataManager.globalFilter || datasetId < 0) {
            return
        }

        let allIds = dataManager.getAllDatasetIds()
        if (allIds.length === 0) {
            return
        }

        let visibleIds = visibleDatasetIds()
        let shouldShow = !containsId(visibleIds, datasetId)
        let nextIds = []
        for (let i = 0; i < allIds.length; ++i) {
            let id = allIds[i]
            if (id === datasetId) {
                if (shouldShow) {
                    nextIds.push(id)
                }
            } else if (containsId(visibleIds, id)) {
                nextIds.push(id)
            }
        }

        activeDatasetIds = nextIds
        datasetFilterInverted = false

        if (nextIds.length === allIds.length) {
            datasetFilterEnabled = false
            dataManager.globalFilter.setFilterEnabled(GlobalFilter.FilterType.Dataset, false)
            dataManager.globalFilter.clearFilter(GlobalFilter.FilterType.Dataset)
        } else {
            datasetFilterEnabled = true
            dataManager.globalFilter.setFilter(GlobalFilter.FilterType.Dataset, nextIds)
            dataManager.globalFilter.setFilterEnabled(GlobalFilter.FilterType.Dataset, true)
        }
    }

    function handleDatasetClicked(row, button, modifiers) {
        view.forceActiveFocus()
        if (!datasets || !selection || row < 0) {
            return
        }

        let item = view.itemAtIndex(row)
        if (!item) {
            return
        }

        let modelIndex = datasets.index(row, 0)
        if (datasets.lastIndex === -1) {
            datasets.lastIndex = row
        }

        if (button === Qt.LeftButton || (button === Qt.RightButton && !selection.isSelected(modelIndex))) {
            if (modifiers & Qt.ShiftModifier) {
                datasets.shiftSelect(row, datasets.lastIndex, ItemSelectionModel.ClearAndSelect)
                selection.setCurrentIndex(modelIndex, ItemSelectionModel.Select)
            } else if (modifiers & Qt.ControlModifier) {
                selection.select(modelIndex, ItemSelectionModel.Select)
                selection.setCurrentIndex(modelIndex, ItemSelectionModel.Select)
            } else {
                selection.select(modelIndex, ItemSelectionModel.ClearAndSelect)
                selection.setCurrentIndex(modelIndex, ItemSelectionModel.Select)
                datasets.lastIndex = row
            }
        }

        curItem = item
        if (button === Qt.RightButton) {
            menu.popup()
        }
    }

    onDataManagerChanged: syncDatasetFilterState()

    Component.onCompleted: syncDatasetFilterState()

    QuiContentDialog {
        id: deleteConfirmDialog
        title: "删除数据集"
        message: selectedCount > 1 ? "确定删除选中的 " + selectedCount + " 个数据集吗?"
                                  : "确定删除选中的数据集吗?"
        onPositiveClicked: function () {
            let ids = selectedDatasetIds()
            if (dataManager && ids.length > 0) {
                dataManager.deleteDatasets(ids)
                curItem = null
            }
        }
    }

    QuiMenu {
        id: menu
        width: 200
        QuiMenuItem {
            text: "导入"
            iconSource: QuiFontIcon.ImportMirrored
            enabled: selectedCount === 1 && hasCurItem()
            onClicked: {
                if (dataManager && curItem) {
                    importDataDialog.datasetsModel = dataManager.getAllDatasetsName()
                    importDataDialog.datasetName = curItem.name
                    importDataDialog.open()
                }
            }
        }
        QuiMenuItem {
            text: "导出"
            iconSource: QuiFontIcon.Export
            enabled: selectedCount > 0
            onClicked: {
                let ids = selectedDatasetIds()
                if (dataManager && ids.length > 0) {
                    exportDataDialog.datasetIds = ids
                    exportDataDialog.datasetName = ids.length === 1 && curItem ? curItem.name : ids.length + " 个数据集"
                    exportDataDialog.open()
                }
            }
        }
        QuiMenuItem {
            text: "导出全部"
            iconSource: QuiFontIcon.Export
            enabled: dataManager && datasets && datasets.rowCount() > 0
            onClicked: {
                let ids = dataManager.getAllDatasetIds()
                if (dataManager && ids.length > 0) {
                    exportDataDialog.datasetIds = ids
                    exportDataDialog.datasetName = "全部数据集"
                    exportDataDialog.open()
                }
            }
        }
        QuiMenuItem {
            text: "修改"
            iconSource: QuiFontIcon.Edit
            enabled: selectedCount === 1 && hasCurItem()
            onClicked: {
                if (curItem) {
                    editor.text = ""
                    let pos = curItem.mapToItem(null, 0, 0)
                    editor.x = pos.x + curItem.width
                    editor.y = pos.y + 10
                    editor.open()
                }
            }
        }
        QuiMenuItem {
            text: "图像聚类"
            iconSource: QuiFontIcon.AreaChart
            enabled: dataManager
                     && featureManager
                     && featureManager.imageCluster
                     && featureManager.imageCluster.enabled
                     && !featureManager.imageCluster.running
                     && selectedCount === 1
                     && hasCurItem()
            onClicked: {
                if (curItem) {
                    imageClusterDialog.openForDatasets([curItem.dataset_id])
                }
            }
        }
        QuiMenuItem {
            text: "删除"
            iconSource: QuiFontIcon.Delete
            enabled: selectedCount > 0
            onClicked: {
                deleteConfirmDialog.open()
            }
        }
    }

    QuiEditor {
        id: editor
        description: "输入数据集名称"
        onEditTextChanged: function (datasetName) {
            if (dataManager && curItem
                    && dataManager.isValidDatasetName(datasetName, curItem.dataset_id).length === 0) {
                dataManager.updateDataset(curItem.dataset_id, datasetName)
            }
        }
    }

    DatasetDataIODialog {
        id: importDataDialog
        ioMode: DatasetDataIODialog.Import
        dataManager: datasetsView.dataManager
    }

    DatasetDataIODialog {
        id: exportDataDialog
        ioMode: DatasetDataIODialog.Export
        dataManager: datasetsView.dataManager
    }

    ImageClusterDialog {
        id: imageClusterDialog
        dataManager: datasetsView.dataManager
        featureManager: datasetsView.featureManager
    }

    Connections {
        target: dataManager ? dataManager.globalFilter : null
        function onFilterStateChanged() {
            syncDatasetFilterState()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 0
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        
        DatasetHeader {
            Layout.fillWidth: true
            height: 32
            dataManager: datasetsView.dataManager
        }

        ListView {
            id: view
            clip: true
            focus: true
            Layout.fillHeight: true
            Layout.fillWidth: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            model: datasets
            property int hoveredIndex: -1
            ScrollBar.vertical: QuiScrollBar {}
            delegate: DatasetDelegate {
                height: 32
                width: view.width - 8
                name: model.name
                stats: model.stats
                dataset_id: model.dataset_id
                progress: model.progress
                row: index
                selected: model.selected ? model.selected : false
                hovered: view.hoveredIndex === index
                filterActive: isDatasetFiltered(model.dataset_id)
                onClicked: function(row, button, modifiers) {
                    handleDatasetClicked(row, button, modifiers)
                }
                onFilterClicked: function(datasetId) {
                    toggleDatasetFilter(datasetId)
                }
            }

            MouseArea {
                id: hoverTracker
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                hoverEnabled: true
                z: 10

                function updateHoveredIndex(mouseX, mouseY) {
                    let row = view.indexAt(mouseX + view.contentX, mouseY + view.contentY)
                    view.hoveredIndex = row >= 0 ? row : -1
                }

                onPositionChanged: function(mouse) {
                    updateHoveredIndex(mouse.x, mouse.y)
                }
                onEntered: updateHoveredIndex(mouseX, mouseY)
                onExited: view.hoveredIndex = -1

                Connections {
                    target: view
                    function onContentYChanged() {
                        if (hoverTracker.containsMouse) {
                            hoverTracker.updateHoveredIndex(hoverTracker.mouseX, hoverTracker.mouseY)
                        }
                    }
                }
            }

            Keys.enabled: view.visible
            Keys.onPressed: function(event) {
                if (!datasets || !selection) {
                    return
                }

                if (event.key === Qt.Key_Escape) {
                    selection.clear()
                    curItem = null
                    event.accepted = true
                } else if (event.key === Qt.Key_Delete && selection.hasSelection) {
                    deleteConfirmDialog.open()
                    event.accepted = true
                } else if ((event.key === Qt.Key_A) && (event.modifiers & Qt.ControlModifier)) {
                    datasets.selectAll()
                    event.accepted = true
                } else {
                    updateSelectionByKeyboard(event)
                }
            }

            function updateSelectionByKeyboard(event) {
                if (!datasets || !selection || view.count <= 0) {
                    return
                }

                let curIndex = selection.currentIndex.row
                let newIndex = curIndex < 0 ? 0 : curIndex
                if (event.key === Qt.Key_Up) {
                    newIndex = Math.max(0, newIndex - 1)
                } else if (event.key === Qt.Key_Down) {
                    newIndex = Math.min(view.count - 1, newIndex + 1)
                } else if (event.key === Qt.Key_Home) {
                    newIndex = 0
                } else if (event.key === Qt.Key_End) {
                    newIndex = view.count - 1
                } else {
                    return
                }

                let modelIndex = datasets.index(newIndex, 0)
                selection.select(modelIndex, ItemSelectionModel.ClearAndSelect)
                selection.setCurrentIndex(modelIndex, ItemSelectionModel.Select)
                datasets.lastIndex = newIndex
                view.positionViewAtIndex(newIndex, ListView.Contain)
                event.accepted = true
            }
        }
    }
}
