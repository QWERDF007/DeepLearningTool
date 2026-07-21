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
    property bool roiSearchEnabled: true
    property string validationMessage: ""
    property bool startAttempted: false
    readonly property var roiSearchSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.RoiSearch)

    DataSelectionTreeModel {
        id: datasetSelectionModel
    }

    implicitWidth: 680
    implicitHeight: 760
    focus: true
    closePolicy: Popup.CloseOnEscape

    function roiSearchController() {
        return featureManager ? featureManager.roiSearch : null
    }

    function bindDatasetSelectionModel() {
        let manager = dialog.dataManager
        datasetSelectionModel.setDatasetClassSourceModels(
                    manager ? manager.datasets : null,
                    manager ? manager.labelClasses : null,
                    manager ? manager.imageInstances : null,
                    manager ? manager.labelInstances : null)
    }

    function openForLabels(labelIds) {
        queryLabelIds = labelIds ? labelIds : []
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
        let controller = roiSearchController()
        if (!controller) {
            validationMessage = "标注搜索功能未初始化"
            return
        }
        if (controller.running) {
            validationMessage = "标注搜索正在运行"
            return
        }
        if (!roiSearchEnabled || !controller.enabled) {
            validationMessage = "标注搜索未启用"
            return
        }
        if (!queryLabelIds || queryLabelIds.length === 0) {
            validationMessage = "请先选择要搜索的标注"
            return
        }
        if (selectedSearchScope().length === 0) {
            validationMessage = "请至少选择一个搜索数据集"
            return
        }
        validationMessage = controller.validationError()
    }

    function refreshRoiSearchEnabled() {
        roiSearchEnabled = GlobalSettings.valueForField(
                    SettingsAccessor.RoiSearch,
                    RoiSearchField.Enabled,
                    true)
    }

    function startSearch() {
        let controller = roiSearchController()
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
        resetDatasetSelection()
        refreshRoiSearchEnabled()
        Qt.callLater(updateValidation)
    }

    onDataManagerChanged: {
        bindDatasetSelectionModel()
        updateValidation()
    }
    onQueryLabelIdsChanged: updateValidation()
    onRoiSearchEnabledChanged: updateValidation()
    Component.onCompleted: {
        bindDatasetSelectionModel()
        updateValidation()
    }

    Connections {
        target: roiSearchSettings ? roiSearchSettings.fieldModel : null

        function onValueChanged(name, value) {
            dialog.refreshRoiSearchEnabled()
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
        title: "标注搜索"
        settingsFieldModel: roiSearchSettings ? roiSearchSettings.fieldModel : null
        datasetSectionComponent: Component {
            DatasetSelectionTreeView {
                roleTitle: "搜索数据集"
                selectionModel: datasetSelectionModel
            }
        }
        errorText: dialog.validationMessage.length > 0
                   ? dialog.validationMessage
                   : (dialog.startAttempted && dialog.roiSearchController()
                      ? dialog.roiSearchController().lastError : "")
        primaryButtonText: "开始搜索"
        primaryButtonEnabled: dialog.roiSearchController()
                              && !dialog.roiSearchController().running
                              && dialog.validationMessage.length === 0
        onCancelRequested: dialog.close()
        onPrimaryRequested: dialog.startSearch()
    }
}
