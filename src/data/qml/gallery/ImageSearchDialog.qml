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
    implicitHeight: 720
    focus: true
    closePolicy: Popup.CloseOnEscape

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
        modelBatchEditor.value = GlobalSettings.data.featureExtractionModelBatchSize
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
        GlobalSettings.data.featureExtractionModelBatchSize = Math.round(modelBatchEditor.value)
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
                    GlobalSettings.data.featureExtractionModelBatchSize,
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
        width: parent.width
        height: parent.height
        spacing: 0

        // 标题
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 18
            Layout.bottomMargin: 10
            spacing: 10

            DltText {
                Layout.fillWidth: true
                text: "图像搜索"
                font: DltFont.Title
                color: DltColor.FontPrimary
            }
        }

        // 可滚动内容
        DltScrollablePage {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0

            ColumnLayout {
                width: parent.width
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    implicitHeight: searchSection.implicitHeight + 24
                    radius: 4
                    color: DltColor.Primary
                    border.color: DltColor.Border

                    ColumnLayout {
                        id: searchSection
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10
                        enabled: GlobalSettings.data.featureExtractionEnabled

                        // 搜索数据集
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            DltText {
                                text: "搜索数据集"
                                color: DltColor.FontDark
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(150, Math.max(48, datasetColumn.implicitHeight + 12))
                                radius: 4
                                color: DltColor.Background
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
                        }

                        // 模型
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "模型"
                                color: DltColor.FontDark
                            }
                            DltComboBox {
                                id: modelBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
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

                        // 模型路径
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "模型路径"
                                color: DltColor.FontDark
                            }
                            RowLayout {
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
                                spacing: 8

                                DltTextField {
                                    id: weightsPathInput
                                    Layout.fillWidth: true
                                    placeholderText: "选择 .wts 权重文件"
                                    onEditingFinished: {
                                        GlobalSettings.data.featureExtractionModelPath = dialog.trimText(text)
                                    }
                                }
                                DltTextIconButton {
                                    Layout.preferredWidth: 34
                                    Layout.preferredHeight: 34
                                    iconSource: DltFontIcon.OpenFile
                                    text: "打开"
                                    onClicked: weightsFileDialog.open()
                                }
                            }
                        }

                        // 特征层名
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "特征层名"
                                color: DltColor.FontDark
                            }
                            FeatureNameComboBox {
                                id: featureBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
                                imageSearch: dialog.imageSearchController()
                                modelName: dialog.comboText(modelBox)
                                featureName: GlobalSettings.data.featureExtractionFeatureName
                                onFeatureNameAccepted: function (featureName) {
                                    GlobalSettings.data.featureExtractionFeatureName = featureName
                                }
                            }
                        }

                        // 推理后端
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "推理后端"
                                color: DltColor.FontDark
                            }
                            DltComboBox {
                                id: modelBackendBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
                                model: ["tensorrt", "openvino", "onnxruntime"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.data.featureExtractionModelBackend = currentText
                                    }
                                }
                            }
                        }

                        // 推理设备
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "推理设备"
                                color: DltColor.FontDark
                            }
                            DltComboBox {
                                id: modelDeviceBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
                                model: ["gpu", "cpu"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.data.featureExtractionModelDevice = currentText
                                    }
                                }
                            }
                        }

                        // 模型批次
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 32

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "模型批次"
                                color: DltColor.FontDark
                            }
                            DltSpinEditor {
                                id: modelBatchEditor
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
                                label: ""
                                value: 1
                                minValue: 1
                                maxValue: 8192
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.data.featureExtractionModelBatchSize = Math.round(value)
                                    }
                                }
                            }
                        }

                        // 特征库重建
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 24

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "特征库重建"
                                color: DltColor.FontDark
                            }
                            DltToggleSwitch {
                                id: rebuildCheckBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                onToggled: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.data.featureExtractionRebuildIndex = checked
                                    }
                                }
                            }
                        }

                        // TopK
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 32

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "TopK"
                                color: DltColor.FontDark
                            }
                            DltSpinEditor {
                                id: topKEditor
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
                                label: ""
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
                        }

                        // 磁盘批次
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 32

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "磁盘批次"
                                color: DltColor.FontDark
                            }
                            DltSpinEditor {
                                id: diskBatchEditor
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
                                label: ""
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

                        // 归一化
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "归一化"
                                color: DltColor.FontDark
                            }
                            DltComboBox {
                                id: normBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
                                model: ["l2", "l1", "none"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.data.featureExtractionNorm = currentText
                                    }
                                }
                            }
                        }

                        // 预处理
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "预处理"
                                color: DltColor.FontDark
                            }
                            DltComboBox {
                                id: preprocessBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
                                model: ["cpu", "gpu"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.data.featureExtractionPreprocessBackend = currentText
                                    }
                                }
                            }
                        }

                        // Faiss
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "Faiss"
                                color: DltColor.FontDark
                            }
                            DltComboBox {
                                id: faissBackendBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
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

                        // 索引存储
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "索引存储"
                                color: DltColor.FontDark
                            }
                            DltComboBox {
                                id: indexStorageBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
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
            }
        }

        // 底部按钮
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
