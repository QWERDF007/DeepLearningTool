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
    property var queryLabelIds: []
    property bool regionSearchEnabled: true
    property string validationMessage: ""
    property bool startAttempted: false
    readonly property var regionSearchSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.RegionSearch)

    DataSelectionTreeModel {
        id: datasetSelectionModel
        onSelectionChanged: dialog.updateValidation()
    }

    implicitWidth: 680
    implicitHeight: 760
    focus: true
    closePolicy: Popup.CloseOnEscape

    function regionSearchController() {
        return featureManager ? featureManager.regionSearch : null
    }

    function bindDatasetSelectionModel() {
        let manager = dialog.dataManager
        datasetSelectionModel.setDatasetClassSourceModels(
                    manager ? manager.datasets : null,
                    manager ? manager.labelClasses : null,
                    manager ? manager.imageSource : null,
                    manager ? manager.labelSource : null)
    }

    function openForLabel(labelId) {
        queryLabelIds = (labelId !== undefined && labelId !== null && labelId >= 0) ? [labelId] : []
        let controller = regionSearchController()
        if (controller && queryLabelIds.length > 0) {
            controller.captureQuery(queryLabelIds[0])
        }
        resetDatasetSelection()
        open()
    }

    function openForLabels(labelIds) {
        queryLabelIds = labelIds ? labelIds : []
        let controller = regionSearchController()
        if (controller && queryLabelIds.length > 0) {
            controller.captureQuery(queryLabelIds[0])
        }
        resetDatasetSelection()
        open()
    }

    function resetDatasetSelection() {
        datasetSelectionModel.clearSelection()
    }

    function selectedSearchScope() {
        return datasetSelectionModel.selectedDatasetClassScope()
    }

    function updateValidation() {
        startAttempted = false
        let controller = regionSearchController()
        if (!controller) {
            validationMessage = "区域检索功能未初始化"
            return
        }
        if (controller.running) {
            validationMessage = "区域检索正在运行"
            return
        }
        if (!regionSearchEnabled || !controller.enabled) {
            validationMessage = "区域检索未启用"
            return
        }
        if (!queryLabelIds || queryLabelIds.length === 0) {
            validationMessage = "请先选择要检索的标注"
            return
        }
        if (selectedSearchScope().length === 0) {
            validationMessage = "请至少选择一个搜索数据集"
            return
        }
        validationMessage = controller.validationError()
    }

    function refreshRegionSearchEnabled() {
        regionSearchEnabled = GlobalSettings.valueForField(
                    SettingsAccessor.RegionSearch,
                    RegionSearchField.Enabled,
                    true)
    }

    function startSearch() {
        let controller = regionSearchController()
        updateValidation()
        if (!controller || validationMessage.length > 0) {
            return
        }

        startAttempted = true
        let started = controller.search(queryLabelIds, selectedSearchScope())
        if (started) {
            close()
        }
    }

    onOpened: {
        refreshRegionSearchEnabled()
        let controller = regionSearchController()
        if (controller && queryLabelIds && queryLabelIds.length > 0) {
            controller.captureQuery(queryLabelIds[0])
        }
        Qt.callLater(updateValidation)
    }

    onDataManagerChanged: {
        bindDatasetSelectionModel()
        updateValidation()
    }
    onQueryLabelIdsChanged: {
        let controller = regionSearchController()
        if (controller && queryLabelIds && queryLabelIds.length > 0) {
            controller.captureQuery(queryLabelIds[0])
        }
        updateValidation()
    }
    onRegionSearchEnabledChanged: updateValidation()
    Component.onCompleted: {
        bindDatasetSelectionModel()
        updateValidation()
    }

    Connections {
        target: regionSearchSettings ? regionSearchSettings.fieldModel : null

        function onValueChanged(name, value) {
            dialog.refreshRegionSearchEnabled()
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
        title: "区域检索"
        settingsFieldModel: regionSearchSettings ? regionSearchSettings.fieldModel : null
        datasetSectionComponent: Component {
            DatasetSelectionTreeView {
                roleTitle: "搜索数据集"
                selectionModel: datasetSelectionModel
                onSelectionEdited: dialog.updateValidation()
            }
        }
        errorText: dialog.validationMessage.length > 0
                   ? dialog.validationMessage
                   : (dialog.startAttempted && dialog.regionSearchController()
                      ? dialog.regionSearchController().lastError : "")
        primaryButtonText: "开始检索"
        primaryButtonEnabled: dialog.regionSearchController()
                              && !dialog.regionSearchController().running
                              && dialog.validationMessage.length === 0
        onCancelRequested: dialog.close()
        onPrimaryRequested: dialog.startSearch()
    }
}
