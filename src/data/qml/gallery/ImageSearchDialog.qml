import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.data
import dltool.settings

DltPopup {
    id: dialog

    property DataManager dataManager
    property bool syncing: false
    property string lastSuggestedWeightsPath: ""

    implicitWidth: 680
    implicitHeight: contentColumn.implicitHeight
    focus: true

    function trimText(value) {
        return value === undefined || value === null ? "" : String(value).trim()
    }

    function comboText(combo) {
        let text = combo.editable ? trimText(combo.editText) : trimText(combo.currentText)
        if (text === "") {
            text = trimText(combo.currentText)
        }
        return text
    }

    function setComboText(combo, value) {
        let text = trimText(value)
        let index = combo.find(text)
        if (index >= 0) {
            combo.currentIndex = index
        } else {
            combo.currentIndex = -1
        }
        if (combo.editable) {
            combo.editText = text
        }
    }

    function imageSearchController() {
        return dataManager ? dataManager.imageSearch : null
    }

    function suggestedWeightsPath(modelName) {
        let controller = imageSearchController()
        if (controller) {
            return controller.suggestedWeightsPath(modelName)
        }
        return modelName === "" ? "" : "F:/models/" + modelName + ".wts"
    }

    function openForSearch() {
        resetDefaults()
        open()
    }

    function resetDefaults() {
        let controller = imageSearchController()
        syncing = true

        let modelName = GlobalSettings.data.featureExtractionModel
        if (modelName === "" && controller) {
            modelName = controller.defaultModelName
        }
        setComboText(modelBox, modelName)

        let modelPath = GlobalSettings.data.featureExtractionModelPath
        if (modelPath === "") {
            modelPath = suggestedWeightsPath(modelName)
        }
        weightsPathInput.text = modelPath

        let featureName = GlobalSettings.data.featureExtractionFeatureName
        if (featureName === "" && controller) {
            featureName = controller.defaultFeatureName
        }
        featureBox.modelName = modelName
        featureBox.featureName = featureName
        featureBox.refreshFeatureNames()

        rebuildCheckBox.checked = GlobalSettings.data.featureExtractionRebuildIndex
        topKEditor.value = GlobalSettings.data.featureExtractionTopK
        setComboText(normBox, GlobalSettings.data.featureExtractionNorm)
        setComboText(preprocessBox, GlobalSettings.data.featureExtractionPreprocessBackend)
        setComboText(faissBackendBox, GlobalSettings.data.featureExtractionFaissBackend)
        setComboText(indexStorageBox, GlobalSettings.data.featureExtractionIndexStorage)
        diskBatchEditor.value = GlobalSettings.data.featureExtractionDiskBuildBatchSize
        setComboText(modelBackendBox, GlobalSettings.data.featureExtractionModelBackend)
        setComboText(modelDeviceBox, GlobalSettings.data.featureExtractionModelDevice)

        lastSuggestedWeightsPath = suggestedWeightsPath(modelName)
        syncing = false

        Qt.callLater(function () {
            for (let i = 0; i < datasetRepeater.count; ++i) {
                let item = datasetRepeater.itemAt(i)
                if (item) {
                    item.checked = true
                }
            }
        })
    }

    function updateModel(value) {
        if (syncing) {
            return
        }

        let modelName = trimText(value)
        if (modelName === "") {
            return
        }

        let previousSuggested = lastSuggestedWeightsPath
        let nextSuggested = suggestedWeightsPath(modelName)
        GlobalSettings.data.featureExtractionModel = modelName
        featureBox.modelName = modelName
        if (weightsPathInput.text === "" || weightsPathInput.text === previousSuggested) {
            weightsPathInput.text = nextSuggested
            GlobalSettings.data.featureExtractionModelPath = nextSuggested
        }
        lastSuggestedWeightsPath = nextSuggested
        featureBox.refreshFeatureNames()
    }

    function persistSettings() {
        featureBox.rememberCurrentText()
        GlobalSettings.data.featureExtractionModel = comboText(modelBox)
        GlobalSettings.data.featureExtractionModelPath = trimText(weightsPathInput.text)
        GlobalSettings.data.featureExtractionFeatureName = featureBox.currentFeatureText()
        GlobalSettings.data.featureExtractionRebuildIndex = rebuildCheckBox.checked
        GlobalSettings.data.featureExtractionTopK = Math.round(topKEditor.value)
        GlobalSettings.data.featureExtractionNorm = normBox.currentText
        GlobalSettings.data.featureExtractionPreprocessBackend = preprocessBox.currentText
        GlobalSettings.data.featureExtractionFaissBackend = faissBackendBox.currentText
        GlobalSettings.data.featureExtractionIndexStorage = indexStorageBox.currentText
        GlobalSettings.data.featureExtractionDiskBuildBatchSize = Math.round(diskBatchEditor.value)
        GlobalSettings.data.featureExtractionModelBackend = modelBackendBox.currentText
        GlobalSettings.data.featureExtractionModelDevice = modelDeviceBox.currentText
    }

    function selectedDatasetIds() {
        let ids = []
        for (let i = 0; i < datasetRepeater.count; ++i) {
            let item = datasetRepeater.itemAt(i)
            if (item && item.checked) {
                ids.push(item.datasetId)
            }
        }
        return ids
    }

    function startSearch() {
        let controller = imageSearchController()
        if (!controller || !GlobalSettings.data.featureExtractionEnabled) {
            return
        }

        persistSettings()
        let started = controller.searchSelectedImages(
                    selectedDatasetIds(),
                    GlobalSettings.data.featureExtractionModel,
                    GlobalSettings.data.featureExtractionModelPath,
                    GlobalSettings.data.featureExtractionFeatureName,
                    GlobalSettings.data.featureExtractionRebuildIndex,
                    GlobalSettings.data.featureExtractionTopK,
                    GlobalSettings.data.featureExtractionNorm,
                    GlobalSettings.data.featureExtractionPreprocessBackend,
                    GlobalSettings.data.featureExtractionFaissBackend,
                    GlobalSettings.data.featureExtractionIndexStorage,
                    GlobalSettings.data.featureExtractionDiskBuildBatchSize,
                    GlobalSettings.data.featureExtractionModelBackend,
                    GlobalSettings.data.featureExtractionModelDevice)
        if (started) {
            close()
        }
    }

    onOpened: resetDefaults()
    onClosed: {
        if (!syncing) {
            persistSettings()
        }
    }

    ColumnLayout {
        id: contentColumn
        width: parent.width
        spacing: 12

        DltText {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 20
            text: "图像搜索"
            font: DltFont.Title
            wrapMode: Text.WrapAnywhere
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 10

            DltText {
                text: "搜索数据集"
                color: DltColor.FontDark
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(150, Math.max(48, datasetColumn.implicitHeight + 12))
                radius: 4
                color: DltColor.Primary
                border.color: DltColor.Border
                clip: true

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 6
                    contentHeight: datasetColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true

                    Column {
                        id: datasetColumn
                        width: parent.width
                        spacing: 4

                        Repeater {
                            id: datasetRepeater
                            model: dialog.dataManager ? dialog.dataManager.datasets : null
                            delegate: DltCheckBox {
                                property int datasetId: model.dataset_id
                                width: datasetColumn.width
                                text: model.name
                                checked: true
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                enabled: GlobalSettings.data.featureExtractionEnabled

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    DltText {
                        text: "模型"
                        color: DltColor.FontDark
                    }
                    DltComboBox {
                        id: modelBox
                        Layout.fillWidth: true
                        editable: true
                        model: dialog.imageSearchController()
                               ? dialog.imageSearchController().supportedModelPresets()
                               : []
                        onActivated: dialog.updateModel(dialog.comboText(modelBox))
                        onCommit: function (text) {
                            editText = text
                            dialog.updateModel(text)
                        }
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: 220
                    spacing: 4
                    DltText {
                        text: "特征层名"
                        color: DltColor.FontDark
                    }
                    FeatureNameComboBox {
                        id: featureBox
                        Layout.fillWidth: true
                        imageSearch: dialog.imageSearchController()
                        modelName: dialog.comboText(modelBox)
                        featureName: GlobalSettings.data.featureExtractionFeatureName
                        onFeatureNameAccepted: function (featureName) {
                            GlobalSettings.data.featureExtractionFeatureName = featureName
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                enabled: GlobalSettings.data.featureExtractionEnabled

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    DltText {
                        text: "推理后端"
                        color: DltColor.FontDark
                    }
                    DltComboBox {
                        id: modelBackendBox
                        Layout.fillWidth: true
                        model: ["tensorrt", "openvino", "onnxruntime"]
                        onActivated: {
                            if (!dialog.syncing) {
                                GlobalSettings.data.featureExtractionModelBackend = currentText
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    DltText {
                        text: "推理设备"
                        color: DltColor.FontDark
                    }
                    DltComboBox {
                        id: modelDeviceBox
                        Layout.fillWidth: true
                        model: ["gpu", "cpu"]
                        onActivated: {
                            if (!dialog.syncing) {
                                GlobalSettings.data.featureExtractionModelDevice = currentText
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                enabled: GlobalSettings.data.featureExtractionEnabled
                DltText {
                    text: "模型路径"
                    color: DltColor.FontDark
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    DltTextField {
                        id: weightsPathInput
                        Layout.fillWidth: true
                        placeholderText: "选择 .wts 权重文件"
                        onEditingFinished: {
                            GlobalSettings.data.featureExtractionModelPath = dialog.trimText(text)
                        }
                    }
                    DltButton {
                        text: "打开"
                        onClicked: weightsFileDialog.open()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                enabled: GlobalSettings.data.featureExtractionEnabled

                DltCheckBox {
                    id: rebuildCheckBox
                    Layout.preferredWidth: 190
                    text: "重新构建特征库"
                    checked: false
                    onToggled: {
                        if (!dialog.syncing) {
                            GlobalSettings.data.featureExtractionRebuildIndex = checked
                        }
                    }
                }

                DltSpinEditor {
                    id: topKEditor
                    Layout.preferredWidth: 180
                    label: "TopK"
                    value: 5
                    minValue: 1
                    maxValue: 1000
                    step: 1
                    onValueChanged: {
                        if (!dialog.syncing) {
                            GlobalSettings.data.featureExtractionTopK = Math.round(value)
                        }
                    }
                }

                DltSpinEditor {
                    id: diskBatchEditor
                    Layout.fillWidth: true
                    label: "磁盘批次"
                    value: 256
                    minValue: 1
                    maxValue: 8192
                    step: 1
                    onValueChanged: {
                        if (!dialog.syncing) {
                            GlobalSettings.data.featureExtractionDiskBuildBatchSize = Math.round(value)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                enabled: GlobalSettings.data.featureExtractionEnabled

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    DltText {
                        text: "归一化"
                        color: DltColor.FontDark
                    }
                    DltComboBox {
                        id: normBox
                        Layout.fillWidth: true
                        model: ["l2", "l1", "none"]
                        onActivated: {
                            if (!dialog.syncing) {
                                GlobalSettings.data.featureExtractionNorm = currentText
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    DltText {
                        text: "预处理"
                        color: DltColor.FontDark
                    }
                    DltComboBox {
                        id: preprocessBox
                        Layout.fillWidth: true
                        model: ["cpu", "gpu"]
                        onActivated: {
                            if (!dialog.syncing) {
                                GlobalSettings.data.featureExtractionPreprocessBackend = currentText
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    DltText {
                        text: "Faiss"
                        color: DltColor.FontDark
                    }
                    DltComboBox {
                        id: faissBackendBox
                        Layout.fillWidth: true
                        model: ["cpu", "gpu"]
                        onActivated: {
                            if (!dialog.syncing) {
                                GlobalSettings.data.featureExtractionFaissBackend = currentText
                                if (currentText === "gpu") {
                                    dialog.setComboText(indexStorageBox, "ram")
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    DltText {
                        text: "索引存储"
                        color: DltColor.FontDark
                    }
                    DltComboBox {
                        id: indexStorageBox
                        Layout.fillWidth: true
                        enabled: faissBackendBox.currentText !== "gpu"
                        model: ["ram", "disk"]
                        onActivated: {
                            if (!dialog.syncing) {
                                GlobalSettings.data.featureExtractionIndexStorage = currentText
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 10
            spacing: 10

            DltText {
                Layout.fillWidth: true
                text: dialog.imageSearchController() ? dialog.imageSearchController().lastError : ""
                color: "red"
                elide: Text.ElideRight
            }

            DltButton {
                text: "取消"
                onClicked: dialog.close()
            }
            DltButton {
                text: "开始搜索"
                enabled: dialog.imageSearchController()
                         && !dialog.imageSearchController().running
                         && GlobalSettings.data.featureExtractionEnabled
                onClicked: dialog.startSearch()
            }
        }
    }

    FileDialog {
        id: weightsFileDialog
        title: "选择模型权重"
        nameFilters: ["Weights (*.wts *.onnx)", "All files (*)"]
        onAccepted: {
            weightsPathInput.text = Utils.getCleanPath(weightsFileDialog.file.toString())
            GlobalSettings.data.featureExtractionModelPath = weightsPathInput.text
        }
    }
}
