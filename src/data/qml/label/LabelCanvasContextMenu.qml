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

        QuiMenuItem {
            text: "标注搜索"
            enabled: actions.dataManager && actions.roiSearch && !actions.roiSearch.running
                     && actions.roiSearchEnabled && actions.selection && actions.selection.hasSelection
            iconSource: QuiFontIcon.Search
            onClicked: actions.startRoiSearchForSelectedLabels()
        }

        QuiMenuItem {
            text: "标注聚类"
            enabled: actions.dataManager && actions.roiCluster
                     && actions.roiClusterEnabled && actions.selection
                     && actions.selection.hasSelection && !actions.roiCluster.running
                     && actions.roiCluster.enabled
            iconSource: QuiFontIcon.AreaChart
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
