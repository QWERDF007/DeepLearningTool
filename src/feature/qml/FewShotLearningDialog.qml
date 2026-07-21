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
    property string validationMessage: ""
    property bool startAttempted: false
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

    function canStart() {
        return controller && !controller.running && validationMessage.length === 0
    }

    function updateValidation() {
        startAttempted = false
        validationMessage = controller ? controller.validationError() : "小样本学习功能未初始化"
    }

    function startFewShot() {
        updateValidation()
        if (!canStart()) {
            return
        }
        startAttempted = true
        if (controller.startFsSam2()) {
            close()
        }
    }

    onOpened: {
        updateValidation()
    }

    onFeatureManagerChanged: updateValidation()
    Component.onCompleted: updateValidation()

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            dialog.updateValidation()
        }
    }

    Connections {
        target: dialog.controller ? dialog.controller.trainDatasetViewModel : null
        function onSelectionChanged() {
            dialog.updateValidation()
        }
    }

    Connections {
        target: dialog.controller ? dialog.controller.validationDatasetViewModel : null
        function onSelectionChanged() {
            dialog.updateValidation()
        }
    }

    Connections {
        target: dialog.controller ? dialog.controller.testDatasetViewModel : null
        function onSelectionChanged() {
            dialog.updateValidation()
        }
    }

    Connections {
        target: dialog.controller ? dialog.controller.labelClassViewModel : null
        function onSelectionChanged() {
            dialog.updateValidation()
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
        errorText: dialog.validationMessage.length > 0
                   ? dialog.validationMessage
                   : (dialog.startAttempted && controller ? controller.lastError : "")
        primaryButtonText: controller && controller.running ? "运行中" : "启动"
        primaryButtonEnabled: dialog.canStart()
        onCancelRequested: dialog.close()
        onPrimaryRequested: dialog.startFewShot()
    }
}
