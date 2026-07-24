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
    property var initialScope: []
    property bool roiClusterEnabled: true
    property string validationMessage: ""
    property bool startAttempted: false
    readonly property var roiClusterSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.RoiCluster)

    DataSelectionTreeModel {
        id: datasetSelectionModel
    }

    implicitWidth: 680
    implicitHeight: 760
    focus: true
    closePolicy: Popup.CloseOnEscape

    function roiClusterController() {
        return featureManager ? featureManager.roiCluster : null
    }

    function bindDatasetSelectionModel() {
        let manager = dialog.dataManager
        datasetSelectionModel.setDatasetClassSourceModels(
                    manager ? manager.datasets : null,
                    manager ? manager.labelClasses : null,
                    manager ? manager.imageSource : null,
                    manager ? manager.labelSource : null)
    }

    function openForLabels(labelIds) {
        initialScope = scopeForLabels(labelIds)
        resetDatasetSelection()
        open()
    }

    function scopeForLabels(labelIds) {
        let scope = []
        let seen = {}
        if (!dataManager || !labelIds) {
            return scope
        }

        for (let i = 0; i < labelIds.length; ++i) {
            let labelId = Number(labelIds[i])
            let imageId = dataManager.labelImageId(labelId)
            let datasetId = imageId >= 0 ? dataManager.imageDatasetId(imageId) : -1
            let classId = dataManager.labelClassId(labelId)
            if (datasetId < 0 || classId < 0) {
                continue
            }

            let key = datasetId + ":" + classId
            if (!seen[key]) {
                seen[key] = true
                scope.push({"dataset_id": datasetId, "label_class_id": classId})
            }
        }
        return scope
    }

    function resetDatasetSelection() {
        datasetSelectionModel.clearSelection()
        if (!initialScope) {
            return
        }

        for (let i = 0; i < initialScope.length; ++i) {
            let item = initialScope[i]
            if (item && item.dataset_id >= 0) {
                datasetSelectionModel.setNodeSelected(item.dataset_id,
                                                       item.label_class_id >= 0 ? item.label_class_id : -1,
                                                       true)
            }
        }
    }

    function selectedClusterScope() {
        return datasetSelectionModel.selectedDatasetClassScope()
    }

    function updateValidation() {
        startAttempted = false
        let controller = roiClusterController()
        if (!controller) {
            validationMessage = "标注聚类功能未初始化"
            return
        }
        if (controller.running) {
            validationMessage = "标注聚类正在运行"
            return
        }
        if (!roiClusterEnabled || !controller.enabled) {
            validationMessage = "标注聚类未启用"
            return
        }
        if (selectedClusterScope().length === 0) {
            validationMessage = "请至少选择一个聚类数据集或类别"
            return
        }
        validationMessage = controller.validationError()
    }

    function refreshRoiClusterEnabled() {
        roiClusterEnabled = GlobalSettings.valueForField(
                    SettingsAccessor.RoiCluster,
                    RoiClusterField.Enabled,
                    true)
    }

    function startCluster() {
        let controller = roiClusterController()
        updateValidation()
        if (!controller || validationMessage.length > 0) {
            return
        }

        startAttempted = true
        if (controller.cluster(selectedClusterScope())) {
            close()
        }
    }

    onOpened: {
        resetDatasetSelection()
        refreshRoiClusterEnabled()
        Qt.callLater(updateValidation)
    }

    onDataManagerChanged: {
        bindDatasetSelectionModel()
        updateValidation()
    }
    onInitialScopeChanged: updateValidation()
    onRoiClusterEnabledChanged: updateValidation()
    onFeatureManagerChanged: updateValidation()
    Component.onCompleted: {
        bindDatasetSelectionModel()
        updateValidation()
    }

    Connections {
        target: roiClusterSettings ? roiClusterSettings.fieldModel : null

        function onValueChanged(name, value) {
            dialog.refreshRoiClusterEnabled()
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
        title: "标注聚类"
        settingsFieldModel: roiClusterSettings ? roiClusterSettings.fieldModel : null
        datasetSectionComponent: Component {
            DatasetSelectionTreeView {
                roleTitle: "聚类数据集/类别"
                selectionModel: datasetSelectionModel
            }
        }
        errorText: dialog.validationMessage.length > 0
                   ? dialog.validationMessage
                   : (dialog.startAttempted && dialog.roiClusterController()
                      ? dialog.roiClusterController().lastError : "")
        primaryButtonText: "开始聚类"
        primaryButtonEnabled: dialog.roiClusterController()
                              && !dialog.roiClusterController().running
                              && dialog.validationMessage.length === 0
        onCancelRequested: dialog.close()
        onPrimaryRequested: dialog.startCluster()
    }
}
