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

        let modelName = GlobalSettings.advanced.imageSearch.model
        if (modelName === "" && controller) {
            modelName = controller.defaultModelName
        }
        setComboText(modelBox, modelName)

        let modelPath = GlobalSettings.advanced.imageSearch.modelPath
        if (modelPath === "") {
            modelPath = suggestedWeightsPath(modelName)
        }
        weightsPathInput.text = modelPath

        let featureName = GlobalSettings.advanced.imageSearch.featureName
        if (featureName === "" && controller) {
            featureName = controller.defaultFeatureName
        }
        featureBox.modelName = modelName
        featureBox.featureName = featureName
        featureBox.refreshFeatureNames()

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
        GlobalSettings.advanced.imageSearch.model = modelName
        featureBox.modelName = modelName
        if (weightsPathInput.text === "" || weightsPathInput.text === previousSuggested) {
            weightsPathInput.text = nextSuggested
            GlobalSettings.advanced.imageSearch.modelPath = nextSuggested
        }
        lastSuggestedWeightsPath = nextSuggested
        featureBox.refreshFeatureNames()
    }

    function persistSettings() {
        featureBox.rememberCurrentText()
        GlobalSettings.advanced.imageSearch.model = comboText(modelBox)
        GlobalSettings.advanced.imageSearch.modelPath = trimText(weightsPathInput.text)
        GlobalSettings.advanced.imageSearch.featureName = featureBox.currentFeatureText()
        GlobalSettings.advanced.imageSearch.rebuildIndex = rebuildCheckBox.checked
        GlobalSettings.advanced.imageSearch.topK = Math.round(topKEditor.value)
        GlobalSettings.advanced.imageSearch.norm = normBox.currentText
        GlobalSettings.advanced.imageSearch.preprocessBackend = preprocessBox.currentText
        GlobalSettings.advanced.imageSearch.faissBackend = faissBackendBox.currentText
        GlobalSettings.advanced.imageSearch.indexStorage = indexStorageBox.currentText
        GlobalSettings.advanced.imageSearch.diskBuildBatchSize = Math.round(diskBatchEditor.value)
        GlobalSettings.advanced.imageSearch.modelBatchSize = Math.round(modelBatchEditor.value)
        GlobalSettings.advanced.imageSearch.modelBackend = modelBackendBox.currentText
        GlobalSettings.advanced.imageSearch.modelDevice = modelDeviceBox.currentText
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
        if (!controller || !GlobalSettings.advanced.imageSearch.enabled) {
            return
        }

        persistSettings()
        let started = controller.searchSelectedImages(
                    selectedDatasetIds(),
                    GlobalSettings.advanced.imageSearch.model,
                    GlobalSettings.advanced.imageSearch.modelPath,
                    GlobalSettings.advanced.imageSearch.featureName,
                    GlobalSettings.advanced.imageSearch.rebuildIndex,
                    GlobalSettings.advanced.imageSearch.topK,
                    GlobalSettings.advanced.imageSearch.norm,
                    GlobalSettings.advanced.imageSearch.preprocessBackend,
                    GlobalSettings.advanced.imageSearch.faissBackend,
                    GlobalSettings.advanced.imageSearch.indexStorage,
                    GlobalSettings.advanced.imageSearch.diskBuildBatchSize,
                    GlobalSettings.advanced.imageSearch.modelBatchSize,
                    GlobalSettings.advanced.imageSearch.modelBackend,
                    GlobalSettings.advanced.imageSearch.modelDevice)
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

            QuiText {
                Layout.fillWidth: true
                text: "图像搜索"
                font: QuiFont.Title
                color: QuiColor.FontPrimary
            }
        }

        // 可滚动内容
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
                        enabled: GlobalSettings.advanced.imageSearch.enabled

                        // 搜索数据集
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            QuiText {
                                text: "\u641c\u7d22\u6570\u636e\u96c6"
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
                                    id: weightsPathInput
                                    Layout.fillWidth: true
                                    placeholderText: "选择 .wts 权重文件"
                                    onEditingFinished: {
                                        GlobalSettings.advanced.imageSearch.modelPath = dialog.trimText(text)
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
                                id: featureBox
                                anchors {
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                }
                                width: parent.width * 2 / 3
                                imageSearch: dialog.imageSearchController()
                                modelName: dialog.comboText(modelBox)
                                featureName: GlobalSettings.advanced.imageSearch.featureName
                                onFeatureNameAccepted: function (featureName) {
                                    GlobalSettings.advanced.imageSearch.featureName = featureName
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
                                text: "\u7279\u5f81\u5e93\u91cd\u5efa"
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
                                value: 5
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
                                value: 256
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

        // 底部按钮
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
                         && GlobalSettings.advanced.imageSearch.enabled
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
            GlobalSettings.advanced.imageSearch.modelPath = weightsPathInput.text
        }
    }
}
