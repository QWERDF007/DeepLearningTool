import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Qt.labs.platform

import dltool.ui
import dltool.data
import dltool.settings
import quickui

Window {
    id: dialog

    property var imageSearch: null
    property var smartAnnotation: null
    property bool advancedExpanded: false
    property bool syncing: false
    property string lastSuggestedWeightsPath: ""
    property string lastSuggestedRoiWeightsPath: ""
    property string lastSuggestedSmartModelPath: ""

    visible: false
    title: "设置"
    width: 1200
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    modality: Qt.NonModal
    flags: Qt.Window | Qt.CustomizeWindowHint | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMaximizeButtonHint | Qt.WindowCloseButtonHint
    color: QuiColor.Background

    onClosing: function(close) {
        if (!syncing) {
            saveVisibleFields()
        }
    }

    onVisibilityChanged: {
        if (visibility === Window.Minimized)
            close()
    }

    Shortcut {
        sequence: "Esc"
        onActivated: dialog.close()
    }

    function screenGeometryFor(targetScreen) {
        if (targetScreen) {
            let availableWidth = targetScreen.desktopAvailableWidth > 0 ? targetScreen.desktopAvailableWidth : targetScreen.width
            let availableHeight = targetScreen.desktopAvailableHeight > 0 ? targetScreen.desktopAvailableHeight : targetScreen.height
            return Qt.rect(targetScreen.virtualX, targetScreen.virtualY, availableWidth, availableHeight)
        }
        return Qt.rect(x, y, width, height)
    }

    function centerInOwner() {
        let owner = transientParent
        let geometry = owner ? Qt.rect(owner.x, owner.y, owner.width, owner.height) : screenGeometryFor(dialog.screen)
        let nextX = Math.round(geometry.x + (geometry.width - width) / 2)
        let nextY = Math.round(geometry.y + (geometry.height - height) / 2)
        let screenGeometry = screenGeometryFor(owner ? owner.screen : dialog.screen)
        let maxX = Math.max(screenGeometry.x, screenGeometry.x + screenGeometry.width - width)
        let maxY = Math.max(screenGeometry.y, screenGeometry.y + screenGeometry.height - height)
        x = Math.max(screenGeometry.x, Math.min(nextX, maxX))
        y = Math.max(screenGeometry.y, Math.min(nextY, maxY))
    }

    function open() {
        if (!visible) {
            loadFromSettings()
            centerInOwner()
        }
        show()
        raise()
        requestActivate()
    }

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

    function suggestedWeightsPath(modelName) {
        if (imageSearch) {
            return imageSearch.suggestedWeightsPath(modelName)
        }
        return modelName === "" ? "" : "F:/models/" + modelName + ".wts"
    }

    function roiFeatureNames(modelName) {
        return imageSearch ? imageSearch.roiFeatureNames(modelName) : []
    }

    function normalizeRoiModel(modelName) {
        let model = trimText(modelName)
        if (model === "" && imageSearch) {
            model = imageSearch.defaultModelName
        }
        if (imageSearch && roiFeatureNames(model).length === 0) {
            model = imageSearch.defaultModelName
        }
        return model
    }

    function normalizeRoiFeature(modelName, featureName) {
        let names = roiFeatureNames(modelName)
        let feature = trimText(featureName)
        if (names.length > 0 && names.indexOf(feature) < 0) {
            return imageSearch ? imageSearch.defaultRoiFeatureName(modelName) : names[names.length - 1]
        }
        if (feature === "" && imageSearch) {
            return imageSearch.defaultRoiFeatureName(modelName)
        }
        return feature
    }

    function suggestedSmartModelPath(modelName, backend) {
        let model = trimText(modelName)
        let runtime = trimText(backend)
        if (smartAnnotation) {
            return smartAnnotation.suggestedModelPath(model, runtime)
        }
        let suffix = runtime === "tensorrt" || runtime === "" ? ".wts" : ".onnx"
        return model === "" ? "" : "F:/models/" + model + suffix
    }

    function loadFromSettings() {
        syncing = true
        advancedExpander.expand = false
        advancedExpanded = false

        enableCheckBox.checked = GlobalSettings.advanced.imageSearch.enabled
        setComboText(modelBox, GlobalSettings.advanced.imageSearch.model)
        modelPathInput.text = GlobalSettings.advanced.imageSearch.modelPath
        featureNameBox.modelName = GlobalSettings.advanced.imageSearch.model
        featureNameBox.featureName = GlobalSettings.advanced.imageSearch.featureName
        featureNameBox.refreshFeatureNames()

        rebuildCheckBox.checked = GlobalSettings.advanced.imageSearch.rebuildIndex
        topKEditor.value = GlobalSettings.advanced.imageSearch.topK
        setComboText(normBox, GlobalSettings.advanced.imageSearch.norm)
        setComboText(preprocessBox, GlobalSettings.advanced.imageSearch.preprocessBackend)
        setComboText(faissBackendBox, GlobalSettings.advanced.imageSearch.faissBackend)
        setComboText(indexStorageBox, GlobalSettings.advanced.imageSearch.indexStorage)
        diskBatchEditor.value = GlobalSettings.advanced.imageSearch.diskBuildBatchSize
        modelBatchEditor.value = GlobalSettings.advanced.imageSearch.modelBatchSize
        setComboText(modelBackendBox, GlobalSettings.advanced.imageSearch.modelBackend)
        setComboText(modelDeviceBox, GlobalSettings.advanced.imageSearch.modelDevice)
        indexDirInput.text = GlobalSettings.advanced.imageSearch.indexDirectory

        lastSuggestedWeightsPath = suggestedWeightsPath(comboText(modelBox))

        let storedRoiModel = trimText(GlobalSettings.advanced.roiSearch.model)
        let roiModel = normalizeRoiModel(storedRoiModel)
        let roiModelPath = GlobalSettings.advanced.roiSearch.modelPath
        if (roiModelPath === "" || (storedRoiModel !== "" && storedRoiModel !== roiModel)) {
            roiModelPath = suggestedWeightsPath(roiModel)
        }
        let roiFeatureName = normalizeRoiFeature(roiModel, GlobalSettings.advanced.roiSearch.featureName)

        roiEnableCheckBox.checked = GlobalSettings.advanced.roiSearch.enabled
        setComboText(roiModelBox, roiModel)
        roiModelPathInput.text = roiModelPath
        roiFeatureNameBox.modelName = roiModel
        roiFeatureNameBox.featureName = roiFeatureName
        roiFeatureNameBox.refreshFeatureNames()
        roiRebuildCheckBox.checked = GlobalSettings.advanced.roiSearch.rebuildIndex
        roiTopKEditor.value = GlobalSettings.advanced.roiSearch.topK
        setComboText(roiNormBox, GlobalSettings.advanced.roiSearch.norm)
        setComboText(roiPreprocessBox, GlobalSettings.advanced.roiSearch.preprocessBackend)
        setComboText(roiFaissBackendBox, GlobalSettings.advanced.roiSearch.faissBackend)
        setComboText(roiIndexStorageBox, GlobalSettings.advanced.roiSearch.indexStorage)
        roiDiskBatchEditor.value = GlobalSettings.advanced.roiSearch.diskBuildBatchSize
        roiModelBatchEditor.value = GlobalSettings.advanced.roiSearch.modelBatchSize
        setComboText(roiModelBackendBox, GlobalSettings.advanced.roiSearch.modelBackend)
        setComboText(roiModelDeviceBox, GlobalSettings.advanced.roiSearch.modelDevice)
        roiIndexDirInput.text = GlobalSettings.advanced.roiSearch.indexDirectory
        roiPooledHeightEditor.value = GlobalSettings.advanced.roiSearch.pooledHeight
        roiPooledWidthEditor.value = GlobalSettings.advanced.roiSearch.pooledWidth
        roiSamplingRatioEditor.value = GlobalSettings.advanced.roiSearch.samplingRatio
        roiAlignedSwitch.checked = GlobalSettings.advanced.roiSearch.aligned
        roiUsePcaSwitch.checked = GlobalSettings.advanced.roiSearch.usePca
        roiPcaDimEditor.value = normalizedPcaDim(GlobalSettings.advanced.roiSearch.pcaDim)
        lastSuggestedRoiWeightsPath = suggestedWeightsPath(roiModel)

        smartEnableCheckBox.checked = GlobalSettings.advanced.smartAnnotation.enabled
        setComboText(smartModelBox, GlobalSettings.advanced.smartAnnotation.model)
        smartModelPathInput.text = GlobalSettings.advanced.smartAnnotation.modelPath
        setComboText(smartBackendBox, GlobalSettings.advanced.smartAnnotation.modelBackend)
        setComboText(smartDeviceBox, GlobalSettings.advanced.smartAnnotation.modelDevice)
        smartThresholdEditor.value = GlobalSettings.advanced.smartAnnotation.maskThreshold
        smartSimplifyEditor.value = GlobalSettings.advanced.smartAnnotation.polygonSimplifyEpsilon
        smartAlphaEditor.value = GlobalSettings.advanced.smartAnnotation.maskAlpha
        smartRefreshIntervalEditor.value = GlobalSettings.advanced.smartAnnotation.refreshInterval
        lastSuggestedSmartModelPath = suggestedSmartModelPath(comboText(smartModelBox), comboText(smartBackendBox))
        syncing = false
    }

    function updateModel(value) {
        if (syncing) {
            return
        }

        let model = trimText(value)
        if (model === "") {
            return
        }

        let previousSuggested = lastSuggestedWeightsPath
        let nextSuggested = suggestedWeightsPath(model)
        GlobalSettings.advanced.imageSearch.model = model
        featureNameBox.modelName = model
        if (modelPathInput.text === "" || modelPathInput.text === previousSuggested) {
            modelPathInput.text = nextSuggested
            GlobalSettings.advanced.imageSearch.modelPath = nextSuggested
        }
        lastSuggestedWeightsPath = nextSuggested
        featureNameBox.refreshFeatureNames()
    }

    function updateSmartModel(value) {
        if (syncing) {
            return
        }

        let model = trimText(value)
        if (model === "") {
            return
        }

        let previousSuggested = lastSuggestedSmartModelPath
        let nextSuggested = suggestedSmartModelPath(model, comboText(smartBackendBox))
        GlobalSettings.advanced.smartAnnotation.model = model
        if (smartModelPathInput.text === "" || smartModelPathInput.text === previousSuggested) {
            smartModelPathInput.text = nextSuggested
            GlobalSettings.advanced.smartAnnotation.modelPath = nextSuggested
        }
        lastSuggestedSmartModelPath = nextSuggested
    }

    function updateRoiModel(value) {
        if (syncing) {
            return
        }

        let rawModel = trimText(value)
        if (rawModel === "") {
            return
        }
        let model = normalizeRoiModel(rawModel)
        if (model !== rawModel) {
            setComboText(roiModelBox, model)
        }

        let previousSuggested = lastSuggestedRoiWeightsPath
        let nextSuggested = suggestedWeightsPath(model)
        GlobalSettings.advanced.roiSearch.model = model
        roiFeatureNameBox.modelName = model
        roiFeatureNameBox.featureName = normalizeRoiFeature(model, roiFeatureNameBox.currentFeatureText())
        if (roiModelPathInput.text === "" || roiModelPathInput.text === previousSuggested) {
            roiModelPathInput.text = nextSuggested
            GlobalSettings.advanced.roiSearch.modelPath = nextSuggested
        }
        lastSuggestedRoiWeightsPath = nextSuggested
        roiFeatureNameBox.refreshFeatureNames()
    }

    function updateSmartBackend(value) {
        if (syncing) {
            return
        }

        let backend = trimText(value)
        let previousSuggested = lastSuggestedSmartModelPath
        let nextSuggested = suggestedSmartModelPath(comboText(smartModelBox), backend)
        GlobalSettings.advanced.smartAnnotation.modelBackend = backend
        if (smartModelPathInput.text === "" || smartModelPathInput.text === previousSuggested) {
            smartModelPathInput.text = nextSuggested
            GlobalSettings.advanced.smartAnnotation.modelPath = nextSuggested
        }
        lastSuggestedSmartModelPath = nextSuggested
    }

    function updateFeatureName(value) {
        if (syncing) {
            return
        }

        let featureName = trimText(value)
        if (featureName === "") {
            return
        }
        GlobalSettings.advanced.imageSearch.featureName = featureName
    }

    function updateRoiFeatureName(value) {
        if (syncing) {
            return
        }

        let featureName = normalizeRoiFeature(comboText(roiModelBox), value)
        if (featureName === "") {
            return
        }
        GlobalSettings.advanced.roiSearch.featureName = featureName
    }

    function saveVisibleFields() {
        featureNameBox.rememberCurrentText()
        roiFeatureNameBox.rememberCurrentText()
        GlobalSettings.advanced.imageSearch.model = comboText(modelBox)
        GlobalSettings.advanced.imageSearch.modelPath = trimText(modelPathInput.text)
        GlobalSettings.advanced.imageSearch.featureName = featureNameBox.currentFeatureText()
        let roiModel = normalizeRoiModel(comboText(roiModelBox))
        let roiFeatureName = normalizeRoiFeature(roiModel, roiFeatureNameBox.currentFeatureText())
        GlobalSettings.advanced.roiSearch.enabled = roiEnableCheckBox.checked
        GlobalSettings.advanced.roiSearch.model = roiModel
        GlobalSettings.advanced.roiSearch.modelPath = trimText(roiModelPathInput.text)
        GlobalSettings.advanced.roiSearch.featureName = roiFeatureName
        GlobalSettings.advanced.roiSearch.rebuildIndex = roiRebuildCheckBox.checked
        GlobalSettings.advanced.roiSearch.topK = Math.round(roiTopKEditor.value)
        GlobalSettings.advanced.roiSearch.norm = comboText(roiNormBox)
        GlobalSettings.advanced.roiSearch.preprocessBackend = comboText(roiPreprocessBox)
        GlobalSettings.advanced.roiSearch.faissBackend = comboText(roiFaissBackendBox)
        GlobalSettings.advanced.roiSearch.indexStorage = comboText(roiIndexStorageBox)
        GlobalSettings.advanced.roiSearch.diskBuildBatchSize = Math.round(roiDiskBatchEditor.value)
        GlobalSettings.advanced.roiSearch.modelBatchSize = Math.round(roiModelBatchEditor.value)
        GlobalSettings.advanced.roiSearch.modelBackend = comboText(roiModelBackendBox)
        GlobalSettings.advanced.roiSearch.modelDevice = comboText(roiModelDeviceBox)
        GlobalSettings.advanced.roiSearch.indexDirectory = trimText(roiIndexDirInput.text)
        GlobalSettings.advanced.roiSearch.pooledHeight = Math.round(roiPooledHeightEditor.value)
        GlobalSettings.advanced.roiSearch.pooledWidth = Math.round(roiPooledWidthEditor.value)
        GlobalSettings.advanced.roiSearch.samplingRatio = Math.round(roiSamplingRatioEditor.value)
        GlobalSettings.advanced.roiSearch.aligned = roiAlignedSwitch.checked
        GlobalSettings.advanced.roiSearch.usePca = roiUsePcaSwitch.checked
        GlobalSettings.advanced.roiSearch.pcaDim = roiUsePcaSwitch.checked ? normalizedPcaDim(roiPcaDimEditor.value) : 0
        GlobalSettings.advanced.smartAnnotation.enabled = smartEnableCheckBox.checked
        GlobalSettings.advanced.smartAnnotation.model = comboText(smartModelBox)
        GlobalSettings.advanced.smartAnnotation.modelPath = trimText(smartModelPathInput.text)
        GlobalSettings.advanced.smartAnnotation.modelBackend = comboText(smartBackendBox)
        GlobalSettings.advanced.smartAnnotation.modelDevice = comboText(smartDeviceBox)
        GlobalSettings.advanced.smartAnnotation.maskThreshold = smartThresholdEditor.value
        GlobalSettings.advanced.smartAnnotation.polygonSimplifyEpsilon = smartSimplifyEditor.value
        GlobalSettings.advanced.smartAnnotation.maskAlpha = smartAlphaEditor.value
        GlobalSettings.advanced.smartAnnotation.refreshInterval = Math.round(smartRefreshIntervalEditor.value)
        GlobalSettings.save()
    }

    ColumnLayout {
        id: contentColumn
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
                text: "设置"
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
                    implicitHeight: featureSection.implicitHeight + 24
                    radius: 4
                    color: QuiColor.Primary
                    border.color: QuiColor.Border

                    ColumnLayout {
                        id: featureSection
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 24

                            QuiText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "图像搜索"
                                font: QuiFont.Subtitle
                                color: QuiColor.FontPrimary
                            }

                            QuiToggleSwitch {
                                id: enableCheckBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                text: "启用"
                                checked: GlobalSettings.advanced.imageSearch.enabled
                                onToggled: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.imageSearch.enabled = checked
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            enabled: enableCheckBox.checked

                            // 模型
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 34

                                QuiText {
                                    anchors {
                                        left: parent.left
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width / 3
                                    text: "模型"
                                    color: QuiColor.FontDark
                                }
                                QuiComboBox {
                                    id: modelBox
                                    anchors {
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width * 2 / 3
                                    editable: true
                                    model: dialog.imageSearch ? dialog.imageSearch.supportedModelPresets() : []
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

                                QuiText {
                                    anchors {
                                        left: parent.left
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width / 3
                                    text: "模型路径"
                                    color: QuiColor.FontDark
                                }
                                RowLayout {
                                    anchors {
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width * 2 / 3
                                    spacing: 8

                                    QuiTextField {
                                        id: modelPathInput
                                        Layout.fillWidth: true
                                        placeholderText: "选择 .wts 权重文件"
                                        onEditingFinished: {
                                            if (!dialog.syncing) {
                                                GlobalSettings.advanced.imageSearch.modelPath = dialog.trimText(text)
                                            }
                                        }
                                    }

                                    QuiTextIconButton {
                                        Layout.preferredWidth: 34
                                        Layout.preferredHeight: 34
                                        iconSource: QuiFontIcon.OpenFile
                                        text: "打开"
                                        onClicked: modelPathDialog.open()
                                    }
                                }
                            }

                            // 特征层名
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 34

                                QuiText {
                                    anchors {
                                        left: parent.left
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width / 3
                                    text: "特征层名"
                                    color: QuiColor.FontDark
                                }
                                FeatureNameComboBox {
                                    id: featureNameBox
                                    anchors {
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width * 2 / 3
                                    imageSearch: dialog.imageSearch
                                    modelName: dialog.comboText(modelBox)
                                    featureName: GlobalSettings.advanced.imageSearch.featureName
                                    onFeatureNameAccepted: function (featureName) {
                                        dialog.updateFeatureName(featureName)
                                    }
                                }
                            }

                            // 推理后端
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 34

                                QuiText {
                                    anchors {
                                        left: parent.left
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width / 3
                                    text: "推理后端"
                                    color: QuiColor.FontDark
                                }
                                QuiComboBox {
                                    id: modelBackendBox
                                    anchors {
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width * 2 / 3
                                    model: ["tensorrt", "openvino", "onnxruntime"]
                                    onActivated: {
                                        if (!dialog.syncing) {
                                            GlobalSettings.advanced.imageSearch.modelBackend = currentText
                                        }
                                    }
                                }
                            }

                            // 推理设备
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 34

                                QuiText {
                                    anchors {
                                        left: parent.left
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width / 3
                                    text: "推理设备"
                                    color: QuiColor.FontDark
                                }
                                QuiComboBox {
                                    id: modelDeviceBox
                                    anchors {
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width * 2 / 3
                                    model: ["gpu", "cpu"]
                                    onActivated: {
                                        if (!dialog.syncing) {
                                            GlobalSettings.advanced.imageSearch.modelDevice = currentText
                                        }
                                    }
                                }
                            }

                            // 模型批次
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 32

                                QuiText {
                                    anchors {
                                        left: parent.left
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width / 3
                                    text: "模型批次"
                                    color: QuiColor.FontDark
                                }
                                QuiSpinEditor {
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
                                            GlobalSettings.advanced.imageSearch.modelBatchSize = Math.round(value)
                                        }
                                    }
                                }
                            }

                            // 特征库目录
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 34

                                QuiText {
                                    anchors {
                                        left: parent.left
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width / 3
                                    text: "特征库目录"
                                    color: QuiColor.FontDark
                                }
                                RowLayout {
                                    anchors {
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width * 2 / 3
                                    spacing: 8

                                    QuiTextField {
                                        id: indexDirInput
                                        Layout.fillWidth: true
                                        placeholderText: "留空则使用项目目录"
                                        text: GlobalSettings.advanced.imageSearch.indexDirectory
                                        onEditingFinished: {
                                            if (!dialog.syncing) {
                                                GlobalSettings.advanced.imageSearch.indexDirectory = dialog.trimText(text)
                                            }
                                        }
                                    }

                                    QuiTextIconButton {
                                        Layout.preferredWidth: 34
                                        Layout.preferredHeight: 34
                                        iconSource: QuiFontIcon.OpenFile
                                        text: "选择"
                                        onClicked: indexDirDialog.open()
                                    }
                                }
                            }
                        }

                        QuiExpander {
                            id: advancedExpander
                            Layout.fillWidth: true
                            headerText: "高级设置"
                            contentHeight: 310
                            enabled: enableCheckBox.checked
                            onExpandChanged: dialog.advancedExpanded = expand

                            content: ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8

                                // 特征库重建
                                Item {
                                    Layout.fillWidth: true
                                    implicitHeight: 24

                                    QuiText {
                                        anchors {
                                            left: parent.left
                                            verticalCenter: parent.verticalCenter
                                        }
                                        width: parent.width / 3
                                        text: "特征库重建"
                                        color: QuiColor.FontDark
                                    }
                                    QuiToggleSwitch {
                                        id: rebuildCheckBox
                                        anchors {
                                            right: parent.right
                                            verticalCenter: parent.verticalCenter
                                        }
                                        onToggled: {
                                            if (!dialog.syncing) {
                                                GlobalSettings.advanced.imageSearch.rebuildIndex = checked
                                            }
                                        }
                                    }
                                }

                                // TopK
                                Item {
                                    Layout.fillWidth: true
                                    implicitHeight: 32

                                    QuiText {
                                        anchors {
                                            left: parent.left
                                            verticalCenter: parent.verticalCenter
                                        }
                                        width: parent.width / 3
                                        text: "TopK"
                                        color: QuiColor.FontDark
                                    }
                                    QuiSpinEditor {
                                        id: topKEditor
                                        anchors {
                                            right: parent.right
                                            verticalCenter: parent.verticalCenter
                                        }
                                        width: parent.width * 2 / 3
                                        label: ""
                                        minValue: 1
                                        maxValue: 1000
                                        step: 1
                                        onValueChanged: {
                                            if (!dialog.syncing) {
                                                GlobalSettings.advanced.imageSearch.topK = Math.round(value)
                                            }
                                        }
                                    }
                                }

                                // 磁盘批次
                                Item {
                                    Layout.fillWidth: true
                                    implicitHeight: 32

                                    QuiText {
                                        anchors {
                                            left: parent.left
                                            verticalCenter: parent.verticalCenter
                                        }
                                        width: parent.width / 3
                                        text: "磁盘批次"
                                        color: QuiColor.FontDark
                                    }
                                    QuiSpinEditor {
                                        id: diskBatchEditor
                                        anchors {
                                            right: parent.right
                                            verticalCenter: parent.verticalCenter
                                        }
                                        width: parent.width * 2 / 3
                                        label: ""
                                        minValue: 1
                                        maxValue: 8192
                                        step: 1
                                        onValueChanged: {
                                            if (!dialog.syncing) {
                                                GlobalSettings.advanced.imageSearch.diskBuildBatchSize = Math.round(value)
                                            }
                                        }
                                    }
                                }

                                // 归一化
                                Item {
                                    Layout.fillWidth: true
                                    implicitHeight: 34

                                    QuiText {
                                        anchors {
                                            left: parent.left
                                            verticalCenter: parent.verticalCenter
                                        }
                                        width: parent.width / 3
                                        text: "归一化"
                                        color: QuiColor.FontDark
                                    }
                                    QuiComboBox {
                                        id: normBox
                                        anchors {
                                            right: parent.right
                                            verticalCenter: parent.verticalCenter
                                        }
                                        width: parent.width * 2 / 3
                                        model: ["l2", "l1", "none"]
                                        onActivated: {
                                            if (!dialog.syncing) {
                                                GlobalSettings.advanced.imageSearch.norm = currentText
                                            }
                                        }
                                    }
                                }

                                // 预处理
                                Item {
                                    Layout.fillWidth: true
                                    implicitHeight: 34

                                    QuiText {
                                        anchors {
                                            left: parent.left
                                            verticalCenter: parent.verticalCenter
                                        }
                                        width: parent.width / 3
                                        text: "预处理"
                                        color: QuiColor.FontDark
                                    }
                                    QuiComboBox {
                                        id: preprocessBox
                                        anchors {
                                            right: parent.right
                                            verticalCenter: parent.verticalCenter
                                        }
                                        width: parent.width * 2 / 3
                                        model: ["cpu", "gpu"]
                                        onActivated: {
                                            if (!dialog.syncing) {
                                                GlobalSettings.advanced.imageSearch.preprocessBackend = currentText
                                            }
                                        }
                                    }
                                }

                                // Faiss
                                Item {
                                    Layout.fillWidth: true
                                    implicitHeight: 34

                                    QuiText {
                                        anchors {
                                            left: parent.left
                                            verticalCenter: parent.verticalCenter
                                        }
                                        width: parent.width / 3
                                        text: "Faiss"
                                        color: QuiColor.FontDark
                                    }
                                    QuiComboBox {
                                        id: faissBackendBox
                                        anchors {
                                            right: parent.right
                                            verticalCenter: parent.verticalCenter
                                        }
                                        width: parent.width * 2 / 3
                                        model: ["cpu", "gpu"]
                                        onActivated: {
                                            if (!dialog.syncing) {
                                                GlobalSettings.advanced.imageSearch.faissBackend = currentText
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

                                    QuiText {
                                        anchors {
                                            left: parent.left
                                            verticalCenter: parent.verticalCenter
                                        }
                                        width: parent.width / 3
                                        text: "索引存储"
                                        color: QuiColor.FontDark
                                    }
                                    QuiComboBox {
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
                                                GlobalSettings.advanced.imageSearch.indexStorage = currentText
                                            }
                                        }
                                    }
                                }
                            }
                        }

                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    implicitHeight: roiSearchSection.implicitHeight + 24
                    radius: 4
                    color: QuiColor.Primary
                    border.color: QuiColor.Border

                    ColumnLayout {
                        id: roiSearchSection
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 24

                            QuiText {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width / 3
                                text: "标注搜索"
                                font: QuiFont.Subtitle
                                color: QuiColor.FontPrimary
                            }

                            QuiToggleSwitch {
                                id: roiEnableCheckBox
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: "启用"
                                checked: GlobalSettings.advanced.roiSearch.enabled
                                onToggled: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.enabled = checked
                                    }
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 10
                            enabled: roiEnableCheckBox.checked

                            QuiText { text: "模型"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiComboBox {
                                id: roiModelBox
                                Layout.fillWidth: true
                                editable: true
                                model: dialog.imageSearch ? dialog.imageSearch.roiModelPresets() : []
                                onActivated: dialog.updateRoiModel(dialog.comboText(roiModelBox))
                                onCommit: function (text) {
                                    editText = text
                                    dialog.updateRoiModel(text)
                                }
                            }

                            QuiText { text: "模型路径"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                QuiTextField {
                                    id: roiModelPathInput
                                    Layout.fillWidth: true
                                    placeholderText: "选择 .wts 权重文件"
                                    onEditingFinished: {
                                        if (!dialog.syncing) {
                                            GlobalSettings.advanced.roiSearch.modelPath = dialog.trimText(text)
                                        }
                                    }
                                }
                                QuiTextIconButton {
                                    Layout.preferredWidth: 34
                                    Layout.preferredHeight: 34
                                    iconSource: QuiFontIcon.OpenFile
                                    text: "打开"
                                    onClicked: roiModelPathDialog.open()
                                }
                            }

                            QuiText { text: "特征层名"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            FeatureNameComboBox {
                                id: roiFeatureNameBox
                                Layout.fillWidth: true
                                imageSearch: dialog.imageSearch
                                modelName: dialog.comboText(roiModelBox)
                                featureName: GlobalSettings.advanced.roiSearch.featureName
                                roiOnly: true
                                rememberCustomValues: false
                                onFeatureNameAccepted: function (featureName) {
                                    dialog.updateRoiFeatureName(featureName)
                                }
                            }

                            QuiText { text: "推理后端"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiComboBox {
                                id: roiModelBackendBox
                                Layout.fillWidth: true
                                model: ["tensorrt", "openvino", "onnxruntime"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.modelBackend = currentText
                                    }
                                }
                            }

                            QuiText { text: "推理设备"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiComboBox {
                                id: roiModelDeviceBox
                                Layout.fillWidth: true
                                model: ["gpu", "cpu"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.modelDevice = currentText
                                    }
                                }
                            }

                            QuiText { text: "模型批次"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiSpinEditor {
                                id: roiModelBatchEditor
                                Layout.fillWidth: true
                                label: ""
                                minValue: 1
                                maxValue: 8192
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.modelBatchSize = Math.round(value)
                                    }
                                }
                            }

                            QuiText { text: "特征库目录"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                QuiTextField {
                                    id: roiIndexDirInput
                                    Layout.fillWidth: true
                                    placeholderText: "留空则使用项目目录"
                                    onEditingFinished: {
                                        if (!dialog.syncing) {
                                            GlobalSettings.advanced.roiSearch.indexDirectory = dialog.trimText(text)
                                        }
                                    }
                                }
                                QuiTextIconButton {
                                    Layout.preferredWidth: 34
                                    Layout.preferredHeight: 34
                                    iconSource: QuiFontIcon.OpenFile
                                    text: "选择"
                                    onClicked: roiIndexDirDialog.open()
                                }
                            }

                            QuiText { text: "特征库重建"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiToggleSwitch {
                                id: roiRebuildCheckBox
                                onToggled: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.rebuildIndex = checked
                                    }
                                }
                            }

                            QuiText { text: "TopK"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiSpinEditor {
                                id: roiTopKEditor
                                Layout.fillWidth: true
                                label: ""
                                minValue: 1
                                maxValue: 1000
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.topK = Math.round(value)
                                    }
                                }
                            }

                            QuiText { text: "归一化"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiComboBox {
                                id: roiNormBox
                                Layout.fillWidth: true
                                model: ["l2", "l1", "none"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.norm = currentText
                                    }
                                }
                            }

                            QuiText { text: "预处理"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiComboBox {
                                id: roiPreprocessBox
                                Layout.fillWidth: true
                                model: ["cpu", "gpu"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.preprocessBackend = currentText
                                    }
                                }
                            }

                            QuiText { text: "Faiss"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiComboBox {
                                id: roiFaissBackendBox
                                Layout.fillWidth: true
                                model: ["cpu", "gpu"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.faissBackend = currentText
                                        if (currentText === "gpu") {
                                            dialog.setComboText(roiIndexStorageBox, "ram")
                                        }
                                    }
                                }
                            }

                            QuiText { text: "索引存储"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiComboBox {
                                id: roiIndexStorageBox
                                Layout.fillWidth: true
                                enabled: roiFaissBackendBox.currentText !== "gpu"
                                model: ["ram", "disk"]
                                onActivated: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.indexStorage = currentText
                                    }
                                }
                            }

                            QuiText { text: "磁盘批次"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiSpinEditor {
                                id: roiDiskBatchEditor
                                Layout.fillWidth: true
                                label: ""
                                minValue: 1
                                maxValue: 8192
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.diskBuildBatchSize = Math.round(value)
                                    }
                                }
                            }

                            QuiText { text: "ROIAlign高度"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiSpinEditor {
                                id: roiPooledHeightEditor
                                Layout.fillWidth: true
                                label: ""
                                minValue: 1
                                maxValue: 64
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.pooledHeight = Math.round(value)
                                    }
                                }
                            }

                            QuiText { text: "ROIAlign宽度"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiSpinEditor {
                                id: roiPooledWidthEditor
                                Layout.fillWidth: true
                                label: ""
                                minValue: 1
                                maxValue: 64
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.pooledWidth = Math.round(value)
                                    }
                                }
                            }

                            QuiText { text: "采样率"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiSpinEditor {
                                id: roiSamplingRatioEditor
                                Layout.fillWidth: true
                                label: ""
                                minValue: -1
                                maxValue: 32
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.samplingRatio = Math.round(value)
                                    }
                                }
                            }

                            QuiText { text: "Aligned"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiToggleSwitch {
                                id: roiAlignedSwitch
                                onToggled: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.roiSearch.aligned = checked
                                    }
                                }
                            }

                            QuiText { text: "PCA降维"; color: QuiColor.FontDark; Layout.preferredWidth: roiSearchSection.width / 3 }
                            QuiToggleSwitch {
                                id: roiUsePcaSwitch
                                onToggled: {
                                    if (!dialog.syncing) {
                                        if (checked && roiPcaDimEditor.value <= 0) {
                                            roiPcaDimEditor.value = 256
                                        }
                                        GlobalSettings.advanced.roiSearch.usePca = checked
                                        GlobalSettings.advanced.roiSearch.pcaDim = checked ? dialog.normalizedPcaDim(roiPcaDimEditor.value) : 0
                                    }
                                }
                            }

                            QuiText {
                                text: "PCA维度"
                                color: QuiColor.FontDark
                                Layout.preferredWidth: roiSearchSection.width / 3
                            }
                            QuiSpinEditor {
                                id: roiPcaDimEditor
                                Layout.fillWidth: true
                                enabled: roiUsePcaSwitch.checked
                                label: ""
                                minValue: 1
                                maxValue: 8192
                                step: 1
                                onValueChanged: {
                                    if (!dialog.syncing && roiUsePcaSwitch.checked) {
                                        GlobalSettings.advanced.roiSearch.pcaDim = dialog.normalizedPcaDim(value)
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    implicitHeight: smartAnnotationSection.implicitHeight + 24
                    radius: 4
                    color: QuiColor.Primary
                    border.color: QuiColor.Border

                    ColumnLayout {
                        id: smartAnnotationSection
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 24

                            QuiText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "智能标注"
                                font: QuiFont.Subtitle
                                color: QuiColor.FontPrimary
                            }

                            QuiToggleSwitch {
                                id: smartEnableCheckBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                text: "启用"
                                checked: GlobalSettings.advanced.smartAnnotation.enabled
                                onToggled: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.advanced.smartAnnotation.enabled = checked
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    enabled: smartEnableCheckBox.checked

                                    Item {
                                        Layout.fillWidth: true
                                        implicitHeight: 34

                                        QuiText {
                                            anchors {
                                                left: parent.left
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width / 3
                                            text: "模型"
                                            color: QuiColor.FontDark
                                        }
                                        QuiComboBox {
                                            id: smartModelBox
                                            anchors {
                                                right: parent.right
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width * 2 / 3
                                            editable: true
                                            model: dialog.smartAnnotation
                                                   ? dialog.smartAnnotation.supportedModelPresets()
                                                   : ["edge_sam", "sam_vit_b", "sam_vit_l", "sam_vit_h",
                                                      "sam2_hiera_tiny", "sam2_hiera_small",
                                                      "sam2_hiera_base_plus", "sam2_hiera_large",
                                                      "sam2_1_hiera_tiny", "sam2_1_hiera_small",
                                                      "sam2_1_hiera_base_plus", "sam2_1_hiera_large"]
                                            onActivated: dialog.updateSmartModel(dialog.comboText(smartModelBox))
                                            onCommit: function (text) {
                                                editText = text
                                                dialog.updateSmartModel(text)
                                            }
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                        implicitHeight: 34

                                        QuiText {
                                            anchors {
                                                left: parent.left
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width / 3
                                            text: "模型文件"
                                            color: QuiColor.FontDark
                                        }
                                        RowLayout {
                                            anchors {
                                                right: parent.right
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width * 2 / 3
                                            spacing: 8

                                            QuiTextField {
                                                id: smartModelPathInput
                                                Layout.fillWidth: true
                                                placeholderText: "选择 .wts 或 .onnx 模型文件"
                                                onEditingFinished: {
                                                    if (!dialog.syncing) {
                                                        GlobalSettings.advanced.smartAnnotation.modelPath = dialog.trimText(text)
                                                    }
                                                }
                                            }

                                            QuiTextIconButton {
                                                Layout.preferredWidth: 34
                                                Layout.preferredHeight: 34
                                                iconSource: QuiFontIcon.OpenFile
                                                text: "打开"
                                                onClicked: smartModelPathDialog.open()
                                            }
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                        implicitHeight: 34

                                        QuiText {
                                            anchors {
                                                left: parent.left
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width / 3
                                            text: "推理后端"
                                            color: QuiColor.FontDark
                                        }
                                        QuiComboBox {
                                            id: smartBackendBox
                                            anchors {
                                                right: parent.right
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width * 2 / 3
                                            model: ["tensorrt", "openvino", "onnxruntime"]
                                            onActivated: dialog.updateSmartBackend(currentText)
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                        implicitHeight: 34

                                        QuiText {
                                            anchors {
                                                left: parent.left
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width / 3
                                            text: "推理设备"
                                            color: QuiColor.FontDark
                                        }
                                        QuiComboBox {
                                            id: smartDeviceBox
                                            anchors {
                                                right: parent.right
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width * 2 / 3
                                            model: ["gpu", "cpu"]
                                            onActivated: {
                                                if (!dialog.syncing) {
                                                    GlobalSettings.advanced.smartAnnotation.modelDevice = currentText
                                                }
                                            }
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                        implicitHeight: 32

                                        QuiText {
                                            anchors {
                                                left: parent.left
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width / 3
                                            text: "Mask阈值"
                                            color: QuiColor.FontDark
                                        }
                                        QuiSpinEditor {
                                            id: smartThresholdEditor
                                            anchors {
                                                right: parent.right
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width * 2 / 3
                                            label: ""
                                            minValue: -20
                                            maxValue: 20
                                            step: 0.1
                                            decimals: 2
                                            onValueChanged: {
                                                if (!dialog.syncing) {
                                                    GlobalSettings.advanced.smartAnnotation.maskThreshold = value
                                                }
                                            }
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                        implicitHeight: 32

                                        QuiText {
                                            anchors {
                                                left: parent.left
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width / 3
                                            text: "刷新间隔(ms)"
                                            color: QuiColor.FontDark
                                        }
                                        QuiSpinEditor {
                                            id: smartRefreshIntervalEditor
                                            anchors {
                                                right: parent.right
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width * 2 / 3
                                            label: ""
                                            minValue: 20
                                            maxValue: 5000
                                            step: 10
                                            decimals: 0
                                            onValueChanged: {
                                                if (!dialog.syncing) {
                                                    GlobalSettings.advanced.smartAnnotation.refreshInterval = Math.round(value)
                                                }
                                            }
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                        implicitHeight: 32

                                        QuiText {
                                            anchors {
                                                left: parent.left
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width / 3
                                            text: "Mask透明度"
                                            color: QuiColor.FontDark
                                        }
                                        QuiSpinEditor {
                                            id: smartAlphaEditor
                                            anchors {
                                                right: parent.right
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width * 2 / 3
                                            label: ""
                                            minValue: 0
                                            maxValue: 1
                                            step: 0.05
                                            decimals: 2
                                            onValueChanged: {
                                                if (!dialog.syncing) {
                                                    GlobalSettings.advanced.smartAnnotation.maskAlpha = value
                                                }
                                            }
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                        implicitHeight: 32

                                        QuiText {
                                            anchors {
                                                left: parent.left
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width / 3
                                            text: "轮廓简化"
                                            color: QuiColor.FontDark
                                        }
                                        QuiSpinEditor {
                                            id: smartSimplifyEditor
                                            anchors {
                                                right: parent.right
                                                verticalCenter: parent.verticalCenter
                                            }
                                            width: parent.width * 2 / 3
                                            label: ""
                                            minValue: 0
                                            maxValue: 50
                                            step: 0.5
                                            decimals: 1
                                            onValueChanged: {
                                                if (!dialog.syncing) {
                                                    GlobalSettings.advanced.smartAnnotation.polygonSimplifyEpsilon = value
                                                }
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

            Item {
                Layout.fillWidth: true
            }

            QuiButton {
                text: "关闭"
                onClicked: dialog.close()
            }
        }
    }

    FileDialog {
        id: modelPathDialog
        title: "选择模型权重"
        nameFilters: ["Weights (*.wts *.onnx)", "All files (*)"]
        onAccepted: {
            modelPathInput.text = Utils.getCleanPath(modelPathDialog.file.toString())
            GlobalSettings.advanced.imageSearch.modelPath = modelPathInput.text
        }
    }

    FileDialog {
        id: smartModelPathDialog
        title: "选择智能标注模型"
        nameFilters: ["Model Files (*.wts *.onnx *.xml *.bin)", "All files (*)"]
        onAccepted: {
            smartModelPathInput.text = Utils.getCleanPath(smartModelPathDialog.file.toString())
            GlobalSettings.advanced.smartAnnotation.modelPath = smartModelPathInput.text
        }
    }

    FileDialog {
        id: roiModelPathDialog
        title: "选择标注搜索模型权重"
        nameFilters: ["Weights (*.wts *.onnx)", "All files (*)"]
        onAccepted: {
            roiModelPathInput.text = Utils.getCleanPath(roiModelPathDialog.file.toString())
            GlobalSettings.advanced.roiSearch.modelPath = roiModelPathInput.text
        }
    }

    FolderDialog {
        id: indexDirDialog
        title: "选择特征库保存目录"
        onAccepted: {
            indexDirInput.text = Utils.getCleanPath(indexDirDialog.folder.toString())
            GlobalSettings.advanced.imageSearch.indexDirectory = indexDirInput.text
        }
    }

    FolderDialog {
        id: roiIndexDirDialog
        title: "选择ROI特征库保存目录"
        onAccepted: {
            roiIndexDirInput.text = Utils.getCleanPath(roiIndexDirDialog.folder.toString())
            GlobalSettings.advanced.roiSearch.indexDirectory = roiIndexDirInput.text
        }
    }
}
