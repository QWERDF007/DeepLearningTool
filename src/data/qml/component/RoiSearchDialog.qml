import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.data
import dltool.settings
import quickui

QuiPopup {
    id: dialog

    property DataManager dataManager
    property bool syncing: false
    property string lastSuggestedWeightsPath: ""
    property var queryLabelIds: []

    implicitWidth: 680
    implicitHeight: 760
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

    function normalizedPcaDim(value) {
        let dim = Math.round(Number(value))
        return dim > 0 ? dim : 256
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

    function roiFeatureNames(modelName) {
        let controller = imageSearchController()
        return controller ? controller.roiFeatureNames(modelName) : []
    }

    function normalizeRoiModel(modelName) {
        let controller = imageSearchController()
        let model = trimText(modelName)
        if (model === "" && controller) {
            model = controller.defaultModelName
        }
        if (controller && roiFeatureNames(model).length === 0) {
            model = controller.defaultModelName
        }
        return model
    }

    function normalizeRoiFeature(modelName, featureName) {
        let controller = imageSearchController()
        let names = roiFeatureNames(modelName)
        let feature = trimText(featureName)
        if (names.length > 0 && names.indexOf(feature) < 0) {
            return controller ? controller.defaultRoiFeatureName(modelName) : names[names.length - 1]
        }
        if (feature === "" && controller) {
            return controller.defaultRoiFeatureName(modelName)
        }
        return feature
    }

    function openForLabels(labelIds) {
        queryLabelIds = labelIds ? labelIds : []
        resetDefaults()
        open()
    }

    function resetDefaults() {
        let controller = imageSearchController()
        syncing = true

        let storedModelName = trimText(GlobalSettings.advanced.roiSearch.model)
        let modelName = normalizeRoiModel(storedModelName)
        setComboText(modelBox, modelName)

        let modelPath = GlobalSettings.advanced.roiSearch.modelPath
        if (modelPath === "" || (storedModelName !== "" && storedModelName !== modelName)) {
            modelPath = suggestedWeightsPath(modelName)
        }
        weightsPathInput.text = modelPath

        let featureName = normalizeRoiFeature(modelName, GlobalSettings.advanced.roiSearch.featureName)
        featureBox.modelName = modelName
        featureBox.featureName = featureName
        featureBox.refreshFeatureNames()

        rebuildCheckBox.checked = GlobalSettings.advanced.roiSearch.rebuildIndex
        topKEditor.value = GlobalSettings.advanced.roiSearch.topK
        setComboText(normBox, GlobalSettings.advanced.roiSearch.norm)
        setComboText(preprocessBox, GlobalSettings.advanced.roiSearch.preprocessBackend)
        setComboText(faissBackendBox, GlobalSettings.advanced.roiSearch.faissBackend)
        setComboText(indexStorageBox, GlobalSettings.advanced.roiSearch.indexStorage)
        diskBatchEditor.value = GlobalSettings.advanced.roiSearch.diskBuildBatchSize
        modelBatchEditor.value = GlobalSettings.advanced.roiSearch.modelBatchSize
        setComboText(modelBackendBox, GlobalSettings.advanced.roiSearch.modelBackend)
        setComboText(modelDeviceBox, GlobalSettings.advanced.roiSearch.modelDevice)
        pooledHeightEditor.value = GlobalSettings.advanced.roiSearch.pooledHeight
        pooledWidthEditor.value = GlobalSettings.advanced.roiSearch.pooledWidth
        samplingRatioEditor.value = GlobalSettings.advanced.roiSearch.samplingRatio
        alignedSwitch.checked = GlobalSettings.advanced.roiSearch.aligned
        usePcaSwitch.checked = GlobalSettings.advanced.roiSearch.usePca
        pcaDimEditor.value = normalizedPcaDim(GlobalSettings.advanced.roiSearch.pcaDim)

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

        let rawModelName = trimText(value)
        if (rawModelName === "") {
            return
        }
        let modelName = normalizeRoiModel(rawModelName)
        if (modelName !== rawModelName) {
            setComboText(modelBox, modelName)
        }

        let previousSuggested = lastSuggestedWeightsPath
        let nextSuggested = suggestedWeightsPath(modelName)
        GlobalSettings.advanced.roiSearch.model = modelName
        featureBox.modelName = modelName
        featureBox.featureName = normalizeRoiFeature(modelName, featureBox.currentFeatureText())
        if (weightsPathInput.text === "" || weightsPathInput.text === previousSuggested) {
            weightsPathInput.text = nextSuggested
            GlobalSettings.advanced.roiSearch.modelPath = nextSuggested
        }
        lastSuggestedWeightsPath = nextSuggested
        featureBox.refreshFeatureNames()
    }

    function persistSettings() {
        featureBox.rememberCurrentText()
        let modelName = normalizeRoiModel(comboText(modelBox))
        GlobalSettings.advanced.roiSearch.model = modelName
        GlobalSettings.advanced.roiSearch.modelPath = trimText(weightsPathInput.text)
        GlobalSettings.advanced.roiSearch.featureName = normalizeRoiFeature(modelName, featureBox.currentFeatureText())
        GlobalSettings.advanced.roiSearch.rebuildIndex = rebuildCheckBox.checked
        GlobalSettings.advanced.roiSearch.topK = Math.round(topKEditor.value)
        GlobalSettings.advanced.roiSearch.norm = normBox.currentText
        GlobalSettings.advanced.roiSearch.preprocessBackend = preprocessBox.currentText
        GlobalSettings.advanced.roiSearch.faissBackend = faissBackendBox.currentText
        GlobalSettings.advanced.roiSearch.indexStorage = indexStorageBox.currentText
        GlobalSettings.advanced.roiSearch.diskBuildBatchSize = Math.round(diskBatchEditor.value)
        GlobalSettings.advanced.roiSearch.modelBatchSize = Math.round(modelBatchEditor.value)
        GlobalSettings.advanced.roiSearch.modelBackend = modelBackendBox.currentText
        GlobalSettings.advanced.roiSearch.modelDevice = modelDeviceBox.currentText
        GlobalSettings.advanced.roiSearch.pooledHeight = Math.round(pooledHeightEditor.value)
        GlobalSettings.advanced.roiSearch.pooledWidth = Math.round(pooledWidthEditor.value)
        GlobalSettings.advanced.roiSearch.samplingRatio = Math.round(samplingRatioEditor.value)
        GlobalSettings.advanced.roiSearch.aligned = alignedSwitch.checked
        GlobalSettings.advanced.roiSearch.usePca = usePcaSwitch.checked
        GlobalSettings.advanced.roiSearch.pcaDim = usePcaSwitch.checked ? normalizedPcaDim(pcaDimEditor.value) : 0
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
        if (!controller || !GlobalSettings.advanced.roiSearch.enabled || !queryLabelIds || queryLabelIds.length === 0) {
            return
        }

        persistSettings()
        let started = controller.searchLabelRois(
                    queryLabelIds,
                    selectedDatasetIds(),
                    GlobalSettings.advanced.roiSearch.model,
                    GlobalSettings.advanced.roiSearch.modelPath,
                    GlobalSettings.advanced.roiSearch.featureName,
                    GlobalSettings.advanced.roiSearch.rebuildIndex,
                    GlobalSettings.advanced.roiSearch.topK,
                    GlobalSettings.advanced.roiSearch.norm,
                    GlobalSettings.advanced.roiSearch.preprocessBackend,
                    GlobalSettings.advanced.roiSearch.faissBackend,
                    GlobalSettings.advanced.roiSearch.indexStorage,
                    GlobalSettings.advanced.roiSearch.diskBuildBatchSize,
                    GlobalSettings.advanced.roiSearch.modelBatchSize,
                    GlobalSettings.advanced.roiSearch.modelBackend,
                    GlobalSettings.advanced.roiSearch.modelDevice,
                    GlobalSettings.advanced.roiSearch.pooledHeight,
                    GlobalSettings.advanced.roiSearch.pooledWidth,
                    GlobalSettings.advanced.roiSearch.samplingRatio,
                    GlobalSettings.advanced.roiSearch.aligned,
                    GlobalSettings.advanced.roiSearch.usePca,
                    GlobalSettings.advanced.roiSearch.pcaDim)
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

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 18
            Layout.bottomMargin: 10
            spacing: 10

            QuiText {
                Layout.fillWidth: true
                text: "标注搜索"
                font: QuiFont.Title
                color: QuiColor.FontPrimary
            }
        }

        QuiScrollablePage {
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
                    color: QuiColor.Primary
                    border.color: QuiColor.Border

                    ColumnLayout {
                        id: searchSection
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10
                        enabled: GlobalSettings.advanced.roiSearch.enabled

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            QuiText {
                                text: "搜索数据集"
                                color: QuiColor.FontDark
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(150, Math.max(48, datasetColumn.implicitHeight + 12))
                                radius: 4
                                color: QuiColor.Background
                                border.color: QuiColor.Border
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
                                            delegate: QuiCheckBox {
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

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "模型"
                                color: QuiColor.FontDark
                            }
                            QuiComboBox {
                                id: modelBox
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                editable: true
                                model: dialog.imageSearchController()
                                       ? dialog.imageSearchController().roiModelPresets()
                                       : []
                                onActivated: dialog.updateModel(dialog.comboText(modelBox))
                                onCommit: function (text) {
                                    editText = text
                                    dialog.updateModel(text)
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "模型路径"
                                color: QuiColor.FontDark
                            }
                            RowLayout {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                spacing: 8

                                QuiTextField {
                                    id: weightsPathInput
                                    Layout.fillWidth: true
                                    placeholderText: "选择 .wts 权重文件"
                                    onEditingFinished: {
                                        GlobalSettings.advanced.roiSearch.modelPath = dialog.trimText(text)
                                    }
                                }
                                QuiTextIconButton {
                                    Layout.preferredWidth: 34
                                    Layout.preferredHeight: 34
                                    iconSource: QuiFontIcon.OpenFile
                                    text: "打开"
                                    onClicked: weightsFileDialog.open()
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "特征层名"
                                color: QuiColor.FontDark
                            }
                            FeatureNameComboBox {
                                id: featureBox
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                imageSearch: dialog.imageSearchController()
                                modelName: dialog.comboText(modelBox)
                                featureName: GlobalSettings.advanced.roiSearch.featureName
                                roiOnly: true
                                onFeatureNameAccepted: function (featureName) {
                                    GlobalSettings.advanced.roiSearch.featureName = featureName
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "推理后端"
                                color: QuiColor.FontDark
                            }
                            QuiComboBox {
                                id: modelBackendBox
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                model: ["tensorrt", "openvino", "onnxruntime"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.modelBackend = currentText
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "推理设备"
                                color: QuiColor.FontDark
                            }
                            QuiComboBox {
                                id: modelDeviceBox
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                model: ["gpu", "cpu"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.modelDevice = currentText
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 32

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "模型批次"
                                color: QuiColor.FontDark
                            }
                            QuiSpinEditor {
                                id: modelBatchEditor
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                label: ""
                                value: 1
                                minValue: 1
                                maxValue: 8192
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.modelBatchSize = Math.round(value)
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 24

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "特征库重建"
                                color: QuiColor.FontDark
                            }
                            QuiToggleSwitch {
                                id: rebuildCheckBox
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                onToggled: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.rebuildIndex = checked
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 32

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "TopK"
                                color: QuiColor.FontDark
                            }
                            QuiSpinEditor {
                                id: topKEditor
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                label: ""
                                value: 5
                                minValue: 1
                                maxValue: 1000
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.topK = Math.round(value)
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 32

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "磁盘批次"
                                color: QuiColor.FontDark
                            }
                            QuiSpinEditor {
                                id: diskBatchEditor
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                label: ""
                                value: 256
                                minValue: 1
                                maxValue: 8192
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.diskBuildBatchSize = Math.round(value)
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "归一化"
                                color: QuiColor.FontDark
                            }
                            QuiComboBox {
                                id: normBox
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                model: ["l2", "l1", "none"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.norm = currentText
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "预处理"
                                color: QuiColor.FontDark
                            }
                            QuiComboBox {
                                id: preprocessBox
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                model: ["cpu", "gpu"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.preprocessBackend = currentText
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "Faiss"
                                color: QuiColor.FontDark
                            }
                            QuiComboBox {
                                id: faissBackendBox
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                model: ["cpu", "gpu"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.faissBackend = currentText
                                        if (currentText === "gpu") {
                                            dialog.setComboText(indexStorageBox, "ram")
                                        }
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 34

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "索引存储"
                                color: QuiColor.FontDark
                            }
                            QuiComboBox {
                                id: indexStorageBox
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                enabled: faissBackendBox.currentText !== "gpu"
                                model: ["ram", "disk"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.indexStorage = currentText
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 32

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "ROIAlign高度"
                                color: QuiColor.FontDark
                            }
                            QuiSpinEditor {
                                id: pooledHeightEditor
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                label: ""
                                value: 7
                                minValue: 1
                                maxValue: 64
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.pooledHeight = Math.round(value)
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 32

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "ROIAlign宽度"
                                color: QuiColor.FontDark
                            }
                            QuiSpinEditor {
                                id: pooledWidthEditor
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                label: ""
                                value: 7
                                minValue: 1
                                maxValue: 64
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.pooledWidth = Math.round(value)
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 32

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "采样率"
                                color: QuiColor.FontDark
                            }
                            QuiSpinEditor {
                                id: samplingRatioEditor
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                label: ""
                                value: -1
                                minValue: -1
                                maxValue: 32
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.samplingRatio = Math.round(value)
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 24

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "Aligned"
                                color: QuiColor.FontDark
                            }
                            QuiToggleSwitch {
                                id: alignedSwitch
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                onToggled: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.aligned = checked
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 24

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "PCA降维"
                                color: QuiColor.FontDark
                            }
                            QuiToggleSwitch {
                                id: usePcaSwitch
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                onToggled: {
                                    if (!dialog.syncing) {
                                        if (checked && pcaDimEditor.value <= 0) {
                                            pcaDimEditor.value = 256
                                        }
                                        GlobalSettings.advanced.roiSearch.usePca = checked
                                        GlobalSettings.advanced.roiSearch.pcaDim = checked ? dialog.normalizedPcaDim(pcaDimEditor.value) : 0
                                    }
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 32

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "PCA维度"
                                color: QuiColor.FontDark
                            }
                            QuiSpinEditor {
                                id: pcaDimEditor
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width * 2 / 3
                                enabled: usePcaSwitch.checked
                                label: ""
                                value: 256
                                minValue: 1
                                maxValue: 8192
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing && usePcaSwitch.checked) {
                                        GlobalSettings.advanced.roiSearch.pcaDim = dialog.normalizedPcaDim(value)
                                    }
                                }
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

            QuiText {
                Layout.fillWidth: true
                text: dialog.imageSearchController() ? dialog.imageSearchController().lastError : ""
                color: "red"
                elide: Text.ElideRight
            }

            QuiButton {
                text: "取消"
                onClicked: dialog.close()
            }
            QuiButton {
                text: "开始搜索"
                enabled: dialog.imageSearchController()
                         && !dialog.imageSearchController().running
                         && queryLabelIds
                         && queryLabelIds.length > 0
                         && GlobalSettings.advanced.roiSearch.enabled
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
            GlobalSettings.advanced.roiSearch.modelPath = weightsPathInput.text
        }
    }
}
