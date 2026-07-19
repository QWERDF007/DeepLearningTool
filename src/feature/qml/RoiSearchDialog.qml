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

    function refreshRoiSearchEnabled() {
        roiSearchEnabled = GlobalSettings.valueForField(
                    SettingsAccessor.RoiSearch,
                    RoiSearchField.Enabled,
                    true)
    }

    function startSearch() {
        let controller = roiSearchController()
        if (!controller || !roiSearchEnabled || !queryLabelIds || queryLabelIds.length === 0) {
            return
        }

        let started = controller.search(queryLabelIds, selectedSearchScope())
        if (started) {
            close()
        }
    }

    onOpened: {
        resetDatasetSelection()
        refreshRoiSearchEnabled()
    }

    onDataManagerChanged: bindDatasetSelectionModel()
    Component.onCompleted: bindDatasetSelectionModel()

    Connections {
        target: roiSearchSettings ? roiSearchSettings.fieldModel : null

        function onValueChanged(name, value) {
            dialog.refreshRoiSearchEnabled()
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
        errorText: dialog.roiSearchController() ? dialog.roiSearchController().lastError : ""
        primaryButtonText: "开始搜索"
        primaryButtonEnabled: dialog.roiSearchController()
                              && !dialog.roiSearchController().running
                              && queryLabelIds
                              && queryLabelIds.length > 0
                              && dialog.roiSearchEnabled
                              && dialog.selectedSearchScope().length > 0
        onCancelRequested: dialog.close()
        onPrimaryRequested: dialog.startSearch()
    }
}
