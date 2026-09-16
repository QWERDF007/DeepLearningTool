import QtQuick
import QtQuick.Controls
import QtQml.Models

import dltool.data
import dltool.feature
import quickui

Item {
    id: actions
    width: 0
    height: 0

    property DataManager dataManager: null
    property FeatureManager featureManager: null
    property ImageInstancesModel imageInstances: null
    property ImageLabelsListModel imageLabelsList: null
    property ItemSelectionModel selection: null
    property ImageSearchController imageSearch: null
    property RoiSearchController roiSearch: null
    property RoiClusterController roiCluster: null
    readonly property RegionSearchController regionSearch: featureManager ? featureManager.regionSearch : null
    property bool roiSearchEnabled: true
    property bool roiClusterEnabled: true

    QuiMenu {
        id: labelCanvasMenu
        width: 200

        QuiMenuItem {
            text: "图像搜索"
            enabled: actions.dataManager && actions.imageSearch && !actions.imageSearch.running
                     && actions.imageSearch.enabled && actions.imageInstances
                     && actions.imageInstances.currentImageId >= 0
            iconSource: QuiFontIcon.Search
            onClicked: actions.startImageSearchForCurrentImage()
        }

        QuiMenu {
            title: "标注搜索"
            iconSource: QuiFontIcon.Search
            enabled: true

            QuiMenuItem {
                id: legacyItem
                text: "已有标注搜索"
                enabled: actions.dataManager && actions.roiSearch && !actions.roiSearch.running
                         && actions.roiSearchEnabled && actions.selection && actions.selection.hasSelection
                onClicked: actions.startRoiSearchForSelectedLabels()
            }

            QuiMenuItem {
                id: regionItem
                text: "区域检索并生成标注"
                enabled: actions.dataManager && actions.regionSearch && actions.regionSearch.enabled
                         && !actions.regionSearch.busy && actions.selection && actions.selection.hasSelection
                         && actions.imageLabelsList && actions.imageLabelsList.getSelectedLabelIds().length === 1
                         && actions.regionSearch.canRepresentResultRect
                onClicked: {
                    if (actions.imageLabelsList) {
                        let ids = actions.imageLabelsList.getSelectedLabelIds()
                        if (ids.length === 1) {
                            regionSearchDialog.openForLabel(ids[0])
                        }
                    }
                }
            }
        }

        QuiMenuItem {
            text: "标注聚类"
            enabled: actions.dataManager && actions.roiCluster
                     && actions.roiClusterEnabled && actions.selection
                     && actions.selection.hasSelection && !actions.roiCluster.running
                     && actions.roiCluster.enabled
            iconSource: QuiFontIcon.GridView
            onClicked: actions.startRoiClusterForSelectedLabels()
        }

        QuiMenuItem {
            text: "删除选中标签实例"
            enabled : actions.selection ? actions.selection.hasSelection : false
            iconSource: QuiFontIcon.Delete
            onClicked: actions.deleteSelectedLabels()
        }
    }

    QuiContentDialog {
        id: deleteConfirmDialog
        title: "删除选中标签实例"
        message: "确定删除选中的标签实例吗?"
        onPositiveClicked: function () {
            if (actions.dataManager && actions.imageLabelsList) {
                let labelIds = actions.imageLabelsList.getSelectedLabelIds()
                actions.dataManager.deleteLabels(labelIds)
            }
        }
    }

    ImageSearchDialog {
        id: imageSearchDialog
        dataManager: actions.dataManager
        featureManager: actions.featureManager
    }

    RoiSearchDialog {
        id: roiSearchDialog
        dataManager: actions.dataManager
        featureManager: actions.featureManager
    }

    RegionSearchDialog {
        id: regionSearchDialog
        dataManager: actions.dataManager
        featureManager: actions.featureManager
    }

    RoiClusterDialog {
        id: roiClusterDialog
        dataManager: actions.dataManager
        featureManager: actions.featureManager
        roiClusterEnabled: actions.roiClusterEnabled
    }

    function popup() {
        labelCanvasMenu.popup()
    }

    function deleteSelectedLabels() {
        if (selection && selection.hasSelection) {
            deleteConfirmDialog.open()
        }
    }

    function startImageSearchForCurrentImage() {
        if (!dataManager || !imageSearch || !imageSearch.enabled || !imageInstances
                || imageInstances.currentImageId < 0) {
            return
        }

        imageSearchDialog.openForImages([imageInstances.currentImageId])
    }

    function startRoiSearchForSelectedLabels() {
        if (!dataManager || !roiSearch || !roiSearchEnabled || !imageLabelsList
                || !selection || !selection.hasSelection) {
            return
        }

        let labelIds = imageLabelsList.getSelectedLabelIds()
        if (labelIds.length > 0) {
            roiSearchDialog.openForLabels(labelIds)
        }
    }

    function startRoiClusterForSelectedLabels() {
        if (!dataManager || !roiCluster || !roiClusterEnabled || !imageLabelsList
                || !selection || !selection.hasSelection || roiCluster.running
                || !roiCluster.enabled) {
            return
        }

        let labelIds = imageLabelsList.getSelectedLabelIds()
        if (labelIds.length > 0) {
            roiClusterDialog.openForLabels(labelIds)
        }
    }
}
