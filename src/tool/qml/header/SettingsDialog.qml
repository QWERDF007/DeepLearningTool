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

    property var imageSearch: null
    property var smartAnnotation: null
    property bool advancedExpanded: false
    property bool syncing: false
    property string lastSuggestedWeightsPath: ""
    property string lastSuggestedSmartModelPath: ""

    implicitWidth: 1200
    implicitHeight: 800 // Math.min(1280, contentColumn.implicitHeight)
    focus: true
    closePolicy: Popup.CloseOnEscape

    onOpened: loadFromSettings()
    onClosed: {
        if (!syncing) {
            saveVisibleFields()
        }
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

    function suggestedWeightsPath(modelName) {
        if (imageSearch) {
            return imageSearch.suggestedWeightsPath(modelName)
        }
        return modelName === "" ? "" : "F:/models/" + modelName + ".wts"
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

    function saveVisibleFields() {
        featureNameBox.rememberCurrentText()
        GlobalSettings.advanced.imageSearch.model = comboText(modelBox)
        GlobalSettings.advanced.imageSearch.modelPath = trimText(modelPathInput.text)
        GlobalSettings.advanced.imageSearch.featureName = featureNameBox.currentFeatureText()
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
                                text: "特征提取"
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

    FolderDialog {
        id: indexDirDialog
        title: "选择特征库保存目录"
        onAccepted: {
            indexDirInput.text = Utils.getCleanPath(indexDirDialog.folder.toString())
            GlobalSettings.advanced.imageSearch.indexDirectory = indexDirInput.text
        }
    }
}
