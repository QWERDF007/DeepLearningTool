import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
    readonly property FewShotLearningController controller: featureManager ? featureManager.fewShotLearning : null
    readonly property var fewShotSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.FewShotLearning)
    property string pythonEnvPath: ""
    readonly property var datasetSelectorSpecs: [
        { "title": "训练数据集", "selection": "train" },
        { "title": "验证数据集", "selection": "validation" },
        { "title": "测试数据集", "selection": "test" },
    ]

    implicitWidth: 720
    implicitHeight: 780
    focus: true
    closePolicy: Popup.CloseOnEscape

    function openForStart() {
        if (controller && !controller.running) {
            controller.clearLastError()
        }
        open()
    }

    function datasetSelectionModel(selectionMapName) {
        if (selectionMapName === "train") {
            return controller ? controller.trainDatasetViewModel : null
        }
        if (selectionMapName === "validation") {
            return controller ? controller.validationDatasetViewModel : null
        }
        return controller ? controller.testDatasetViewModel : null
    }

    function selectedCount(viewModel) {
        return viewModel ? viewModel.selectedCount : 0
    }

    function selectedLabelClassCount(viewModel) {
        return viewModel ? viewModel.selectedLabelClassCount : 0
    }

    function canStart() {
        return controller
                && !controller.running
                && selectedCount(controller.trainDatasetViewModel) > 0
                && selectedCount(controller.testDatasetViewModel) > 0
                && selectedLabelClassCount(controller.trainDatasetViewModel) > 0
                && String(pythonEnvPath || "").length > 0
    }

    function startFewShot() {
        if (!canStart()) {
            return
        }
        if (controller.startFsSam2()) {
            close()
        }
    }

    onOpened: {
        refreshSettings()
    }

    function refreshSettings() {
        pythonEnvPath = GlobalSettings.valueForField(
                    SettingsAccessor.Software,
                    SoftwareField.PythonEnvPath,
                    "")
    }

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            dialog.refreshSettings()
        }
    }

    FeatureDialogLayout {
        title: "小样本学习"
        datasetSectionHeight: 640
        settingsFieldModel: fewShotSettings ? fewShotSettings.fieldModel : null
        datasetSectionComponent: Component {
            ColumnLayout {
                spacing: 12

                Repeater {
                    model: dialog.datasetSelectorSpecs

                    delegate: DatasetSelectionTreeView {
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        roleTitle: modelData.title
                        selectionModel: dialog.datasetSelectionModel(modelData.selection)
                    }
                }
            }
        }
        errorText: controller && controller.lastError.length > 0
                   ? controller.lastError
                   : (String(pythonEnvPath || "").length === 0
                      ? "请先在软件设置中配置 Python 环境目录"
                      : "")
        primaryButtonText: controller && controller.running ? "运行中" : "启动"
        primaryButtonEnabled: dialog.canStart()
        onCancelRequested: dialog.close()
        onPrimaryRequested: dialog.startFewShot()
    }
}
