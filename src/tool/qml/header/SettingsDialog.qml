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

        ScrollView {
            id: settingsScroll
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(600, settingsColumn.implicitHeight + 4)
            clip: true

            ColumnLayout {
                id: settingsColumn
                width: settingsScroll.availableWidth
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

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            DltText {
                                Layout.fillWidth: true
                                text: "特征提取"
                                font: DltFont.Subtitle
                                color: DltColor.FontPrimary
                            }

                            DltCheckBox {
                                id: enableCheckBox
                                text: "启用"
                                checked: GlobalSettings.data.featureExtractionEnabled
                                onToggled: {
                                    if (!dialog.syncing) {
                                        GlobalSettings.data.featureExtractionEnabled = checked
                                    }
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: 10
                            columnSpacing: 12
                            enabled: enableCheckBox.checked

                            DltText {
                                text: "模型"
                                color: DltColor.FontDark
                            }
                            DltComboBox {
                                id: modelBox
                                Layout.fillWidth: true
                                editable: true
                                model: dialog.imageSearch ? dialog.imageSearch.supportedModelPresets() : []
                                onActivated: dialog.updateModel(dialog.comboText(modelBox))
                                onCommit: function (text) {
                                    editText = text
                                    dialog.updateModel(text)
                                }
                            }

                            DltText {
                                text: "模型路径"
                                color: DltColor.FontDark
                            }
                            RowLayout {
                                Layout.fillWidth: true
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

                            DltText {
                                text: "特征层名"
                                color: DltColor.FontDark
                            }
                            FeatureNameComboBox {
                                id: featureNameBox
                                Layout.fillWidth: true
                                imageSearch: dialog.imageSearch
                                modelName: dialog.comboText(modelBox)
                                featureName: GlobalSettings.data.featureExtractionFeatureName
                                onFeatureNameAccepted: function (featureName) {
                                    dialog.updateFeatureName(featureName)
                                }
                            }

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

                            DltText {
                                text: "特征库目录"
                                color: DltColor.FontDark
                            }
                            RowLayout {
                                Layout.fillWidth: true
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

                        DltTextIconButton {
                            id: advancedButton
                            Layout.preferredHeight: 32
                            Layout.preferredWidth: 140
                            display: Button.TextBesideIcon
                            iconSource: dialog.advancedExpanded ? DltFontIcon.ChevronDown : DltFontIcon.ChevronRight
                            text: "高级设置"
                            normalColor: DltColor.Button
                            onClicked: dialog.advancedExpanded = !dialog.advancedExpanded
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: dialog.advancedExpanded
                            enabled: enableCheckBox.checked
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                DltCheckBox {
                                    id: rebuildCheckBox
                                    Layout.preferredWidth: 190
                                    text: "重新构建特征库"
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
