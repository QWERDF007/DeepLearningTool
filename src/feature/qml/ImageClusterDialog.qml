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
    property bool imageClusterEnabled: true
    property string validationMessage: ""
    property bool startAttempted: false
    readonly property var imageClusterSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.ImageCluster)

    DataSelectionTreeModel {
        id: datasetSelectionModel
    }

    implicitWidth: 680
    implicitHeight: 760
    focus: true
    closePolicy: Popup.CloseOnEscape

    function imageClusterController() {
        return featureManager ? featureManager.imageCluster : null
    }

    function bindDatasetSelectionModel() {
        let manager = dialog.dataManager
        datasetSelectionModel.setDatasetClassSourceModels(
                    manager ? manager.datasets : null,
                    manager ? manager.labelClasses : null,
                    manager ? manager.imageInstances : null,
                    manager ? manager.labelInstances : null)
    }

    function openForDatasets(datasetIds) {
        let scope = []
        let ids = datasetIds ? datasetIds : []
        for (let i = 0; i < ids.length; ++i) {
            let datasetId = Number(ids[i])
            if (datasetId >= 0) {
                scope.push({"dataset_id": datasetId, "label_class_id": -1})
            }
        }
        initialScope = scope
        resetDatasetSelection()
        open()
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
        let controller = imageClusterController()
        if (!controller) {
            validationMessage = "图像聚类功能未初始化"
            return
        }
        if (controller.running) {
            validationMessage = "图像聚类正在运行"
            return
        }
        if (!imageClusterEnabled || !controller.enabled) {
            validationMessage = "图像聚类未启用"
            return
        }
        if (selectedClusterScope().length === 0) {
            validationMessage = "请至少选择一个聚类数据集或类别"
            return
        }
        validationMessage = controller.validationError()
    }

    function refreshImageClusterEnabled() {
        imageClusterEnabled = GlobalSettings.valueForField(
                    SettingsAccessor.ImageCluster,
                    ImageClusterField.Enabled,
                    true)
    }

    function startCluster() {
        let controller = imageClusterController()
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
        refreshImageClusterEnabled()
        Qt.callLater(updateValidation)
    }

    onDataManagerChanged: {
        bindDatasetSelectionModel()
        updateValidation()
    }
    onInitialScopeChanged: updateValidation()
    onImageClusterEnabledChanged: updateValidation()
    onFeatureManagerChanged: updateValidation()
    Component.onCompleted: {
        bindDatasetSelectionModel()
        updateValidation()
    }

    Connections {
        target: imageClusterSettings ? imageClusterSettings.fieldModel : null

        function onValueChanged(name, value) {
            dialog.refreshImageClusterEnabled()
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
        title: "图像聚类"
        settingsFieldModel: imageClusterSettings ? imageClusterSettings.fieldModel : null
        datasetSectionComponent: Component {
            DatasetSelectionTreeView {
                roleTitle: "聚类数据集"
                selectionModel: datasetSelectionModel
            }
        }
        errorText: dialog.validationMessage.length > 0
                   ? dialog.validationMessage
                   : (dialog.startAttempted && dialog.imageClusterController()
                      ? dialog.imageClusterController().lastError : "")
        primaryButtonText: "开始聚类"
        primaryButtonEnabled: dialog.imageClusterController()
                              && !dialog.imageClusterController().running
                              && dialog.validationMessage.length === 0
        onCancelRequested: dialog.close()
        onPrimaryRequested: dialog.startCluster()
    }
}
