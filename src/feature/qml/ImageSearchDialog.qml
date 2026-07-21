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
    property string validationMessage: ""
    property bool startAttempted: false
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

    function selectedQueryImageIds() {
        if (queryImageIds && queryImageIds.length > 0) {
            return queryImageIds
        }
        return dataManager && dataManager.imageInstances
                ? dataManager.imageInstances.getSelectedImagesId() : []
    }

    function updateValidation() {
        startAttempted = false
        let controller = imageSearchController()
        if (!controller) {
            validationMessage = "图像搜索功能未初始化"
            return
        }
        if (controller.running) {
            validationMessage = "图像搜索正在运行"
            return
        }
        if (!imageSearchEnabled || !controller.enabled) {
            validationMessage = "图像搜索未启用"
            return
        }
        if (selectedQueryImageIds().length === 0) {
            validationMessage = "请先选择要搜索的图像"
            return
        }
        if (selectedSearchScope().length === 0) {
            validationMessage = "请至少选择一个搜索数据集"
            return
        }
        validationMessage = controller.validationError()
    }

    function refreshImageSearchEnabled() {
        imageSearchEnabled = GlobalSettings.valueForField(
                    SettingsAccessor.ImageSearch,
                    ImageSearchField.Enabled,
                    true)
    }

    function startSearch() {
        let controller = imageSearchController()
        updateValidation()
        if (!controller || validationMessage.length > 0) {
            return
        }

        startAttempted = true
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
        Qt.callLater(updateValidation)
    }

    onDataManagerChanged: {
        bindDatasetSelectionModel()
        updateValidation()
    }
    onQueryImageIdsChanged: updateValidation()
    onImageSearchEnabledChanged: updateValidation()
    Component.onCompleted: {
        bindDatasetSelectionModel()
        updateValidation()
    }

    Connections {
        target: imageSearchSettings ? imageSearchSettings.fieldModel : null

        function onValueChanged(name, value) {
            dialog.refreshImageSearchEnabled()
            dialog.updateValidation()
        }
    }

    Connections {
        target: datasetSelectionModel

        function onSelectionChanged() {
            dialog.updateValidation()
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
        errorText: dialog.validationMessage.length > 0
                   ? dialog.validationMessage
                   : (dialog.startAttempted && dialog.imageSearchController()
                      ? dialog.imageSearchController().lastError : "")
        primaryButtonText: "开始搜索"
        primaryButtonEnabled: dialog.imageSearchController()
                              && !dialog.imageSearchController().running
                              && dialog.validationMessage.length === 0
        onCancelRequested: dialog.close()
        onPrimaryRequested: dialog.startSearch()
    }
}
