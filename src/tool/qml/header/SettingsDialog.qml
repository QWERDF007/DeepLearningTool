import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.data
import dltool.settings

DltPopup {
    id: dialog

    property var imageSearch: null
    property bool advancedExpanded: false
    property bool syncing: false
    property string lastSuggestedWeightsPath: ""

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

    function loadFromSettings() {
        syncing = true
        advancedExpander.expand = false
        advancedExpanded = false

        enableCheckBox.checked = GlobalSettings.data.featureExtractionEnabled
        setComboText(modelBox, GlobalSettings.data.featureExtractionModel)
        modelPathInput.text = GlobalSettings.data.featureExtractionModelPath
        featureNameBox.modelName = GlobalSettings.data.featureExtractionModel
        featureNameBox.featureName = GlobalSettings.data.featureExtractionFeatureName
        featureNameBox.refreshFeatureNames()

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
        indexDirInput.text = GlobalSettings.data.featureExtractionIndexDirectory

        lastSuggestedWeightsPath = suggestedWeightsPath(comboText(modelBox))
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
        GlobalSettings.data.featureExtractionModel = model
        featureNameBox.modelName = model
        if (modelPathInput.text === "" || modelPathInput.text === previousSuggested) {
            modelPathInput.text = nextSuggested
            GlobalSettings.data.featureExtractionModelPath = nextSuggested
        }
        lastSuggestedWeightsPath = nextSuggested
        featureNameBox.refreshFeatureNames()
    }

    function updateFeatureName(value) {
        if (syncing) {
            return
        }

        let featureName = trimText(value)
        if (featureName === "") {
            return
        }
        GlobalSettings.data.featureExtractionFeatureName = featureName
    }

    function saveVisibleFields() {
        featureNameBox.rememberCurrentText()
        GlobalSettings.data.featureExtractionModel = comboText(modelBox)
        GlobalSettings.data.featureExtractionModelPath = trimText(modelPathInput.text)
        GlobalSettings.data.featureExtractionFeatureName = featureNameBox.currentFeatureText()
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

            DltText {
                Layout.fillWidth: true
                text: "设置"
                font: DltFont.Title
                color: DltColor.FontPrimary
            }

        }

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
                    implicitHeight: featureSection.implicitHeight + 24
                    radius: 4
                    color: DltColor.Primary
                    border.color: DltColor.Border

                    ColumnLayout {
                        id: featureSection
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 24

                            DltText {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width / 3
                                text: "特征提取"
                                font: DltFont.Subtitle
                                color: DltColor.FontPrimary
                            }

                            DltToggleSwitch {
                                id: enableCheckBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                text: "启用"
                                checked: GlobalSettings.data.featureExtractionEnabled
                                onToggled: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.data.featureExtractionEnabled = checked
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
                                        id: modelPathInput
                                        Layout.fillWidth: true
                                        placeholderText: "选择 .wts 权重文件"
                                        onEditingFinished: {
                                            if (!dialog.syncing) {
                                                GlobalSettings.data.featureExtractionModelPath = dialog.trimText(text)
                                            }
                                        }
                                    }

                                    DltTextIconButton {
                                        Layout.preferredWidth: 34
                                        Layout.preferredHeight: 34
                                        iconSource: DltFontIcon.OpenFile
                                        text: "打开"
                                        onClicked: modelPathDialog.open()
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
                                    id: featureNameBox
                                    anchors {
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width * 2 / 3
                                    imageSearch: dialog.imageSearch
                                    modelName: dialog.comboText(modelBox)
                                    featureName: GlobalSettings.data.featureExtractionFeatureName
                                    onFeatureNameAccepted: function (featureName) {
                                        dialog.updateFeatureName(featureName)
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

                            // 特征库目录
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 34

                                DltText {
                                    anchors {
                                        left: parent.left
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: parent.width / 3
                                    text: "特征库目录"
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
                                        id: indexDirInput
                                        Layout.fillWidth: true
                                        placeholderText: "留空则使用项目目录"
                                        text: GlobalSettings.data.featureExtractionIndexDirectory
                                        onEditingFinished: {
                                            if (!dialog.syncing) {
                                                GlobalSettings.data.featureExtractionIndexDirectory = dialog.trimText(text)
                                            }
                                        }
                                    }

                                    DltTextIconButton {
                                        Layout.preferredWidth: 34
                                        Layout.preferredHeight: 34
                                        iconSource: DltFontIcon.OpenFile
                                        text: "选择"
                                        onClicked: indexDirDialog.open()
                                    }
                                }
                            }
                        }

                        DltExpander {
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

            DltButton {
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
            GlobalSettings.data.featureExtractionModelPath = modelPathInput.text
        }
    }

    FolderDialog {
        id: indexDirDialog
        title: "选择特征库保存目录"
        onAccepted: {
            indexDirInput.text = Utils.getCleanPath(indexDirDialog.folder.toString())
            GlobalSettings.data.featureExtractionIndexDirectory = indexDirInput.text
        }
    }
}
