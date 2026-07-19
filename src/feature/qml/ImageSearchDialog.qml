import QtQuick
import QtQuick.Controls

pragma ComponentBehavior: Bound

import dltool.ui
import dltool.settings
import dltool.data
import dltool.feature
import quickui

QuiPopup {
    id: dialog

    property DataManager dataManager
    property FeatureManager featureManager
    property var queryImageIds: []
    property bool imageSearchEnabled: true
    readonly property var imageSearchSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.ImageSearch)

    DataSelectionTreeModel {
        id: datasetSelectionModel
    }

    implicitWidth: 680
    implicitHeight: 720
    focus: true
    closePolicy: Popup.CloseOnEscape

    function imageSearchController() {
        return featureManager ? featureManager.imageSearch : null
    }

    function bindDatasetSelectionModel() {
        let manager = dialog.dataManager
        datasetSelectionModel.setDatasetClassSourceModels(
                    manager ? manager.datasets : null,
                    manager ? manager.labelClasses : null,
                    manager ? manager.imageInstances : null,
                    manager ? manager.labelInstances : null)
    }

    function openForSearch() {
        queryImageIds = []
        resetDatasetSelection()
        open()
    }

    function openForImages(imageIds) {
        queryImageIds = imageIds ? imageIds : []
        resetDatasetSelection()
        open()
    }

    function resetDatasetSelection() {
        datasetSelectionModel.clearSelection()
    }

    function selectedSearchScope() {
        return datasetSelectionModel.selectedDatasetClassScope()
    }

    function refreshImageSearchEnabled() {
        imageSearchEnabled = GlobalSettings.valueForField(
                    SettingsAccessor.ImageSearch,
                    ImageSearchField.Enabled,
                    true)
    }

    function startSearch() {
        let controller = imageSearchController()
        if (!controller) {
            return
        }

        let searchScope = selectedSearchScope()
        let started = false
        if (queryImageIds && queryImageIds.length > 0) {
            started = controller.search(queryImageIds, searchScope)
        } else {
            started = controller.searchSelectedImages(searchScope)
        }
        if (started) {
            close()
        }
    }

    onOpened: {
        resetDatasetSelection()
        refreshImageSearchEnabled()
    }

    onDataManagerChanged: bindDatasetSelectionModel()
    Component.onCompleted: bindDatasetSelectionModel()

    Connections {
        target: imageSearchSettings ? imageSearchSettings.fieldModel : null

        function onValueChanged(name, value) {
            dialog.refreshImageSearchEnabled()
        }
    }

    FeatureDialogLayout {
        title: "图像搜索"
        settingsFieldModel: imageSearchSettings ? imageSearchSettings.fieldModel : null
        datasetSectionComponent: Component {
            DatasetSelectionTreeView {
                roleTitle: "搜索数据集"
                selectionModel: datasetSelectionModel
            }
        }
        errorText: dialog.imageSearchController() ? dialog.imageSearchController().lastError : ""
        primaryButtonText: "开始搜索"
        primaryButtonEnabled: dialog.imageSearchController()
                              && !dialog.imageSearchController().running
                              && dialog.imageSearchEnabled
                              && dialog.selectedSearchScope().length > 0
        onCancelRequested: dialog.close()
        onPrimaryRequested: dialog.startSearch()
    }
}
