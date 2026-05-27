import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.data

DltPopup {
    id: dialog

    property DataManager dataManager

    implicitWidth: 680
    implicitHeight: contentColumn.implicitHeight
    focus: true

    function openForSearch() {
        resetDefaults()
        open()
    }

    function resetDefaults() {
        if (dataManager && dataManager.imageSearch) {
            if (modelBox.currentText === "") {
                modelBox.currentIndex = 0
            }
            if (featureInput.text === "") {
                featureInput.text = dataManager.imageSearch.defaultFeatureName
            }
            if (weightsPathInput.text === "") {
                weightsPathInput.text = dataManager.imageSearch.suggestedWeightsPath(modelBox.currentText)
            }
        }
        Qt.callLater(function () {
            for (let i = 0; i < datasetRepeater.count; ++i) {
                let item = datasetRepeater.itemAt(i)
                if (item) {
                    item.checked = true
                }
            }
        })
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
        if (!dataManager || !dataManager.imageSearch) {
            return
        }
        let started = dataManager.imageSearch.searchSelectedImages(
                    selectedDatasetIds(),
                    modelBox.currentText,
                    weightsPathInput.text,
                    featureInput.text,
                    rebuildCheckBox.checked,
                    Math.round(topKEditor.value),
                    normBox.currentText,
                    preprocessBox.currentText,
                    faissBackendBox.currentText,
                    indexStorageBox.currentText,
                    Math.round(diskBatchEditor.value))
        if (started) {
            close()
        }
    }

    onOpened: resetDefaults()

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
                        model: dialog.dataManager && dialog.dataManager.imageSearch
                               ? dialog.dataManager.imageSearch.supportedModelPresets()
                               : []
                        Component.onCompleted: currentIndex = 0
                        onActivated: {
                            if (dialog.dataManager && dialog.dataManager.imageSearch) {
                                weightsPathInput.text = dialog.dataManager.imageSearch.suggestedWeightsPath(currentText)
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: 220
                    spacing: 4
                    DltText {
                        text: "特征层"
                        color: DltColor.FontDark
                    }
                    DltTextField {
                        id: featureInput
                        Layout.fillWidth: true
                        text: dialog.dataManager && dialog.dataManager.imageSearch
                              ? dialog.dataManager.imageSearch.defaultFeatureName
                              : "layer4"
                        placeholderText: "layer4 / x_norm_clstoken"
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
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

                DltCheckBox {
                    id: rebuildCheckBox
                    Layout.preferredWidth: 190
                    text: "重新构建特征库"
                    checked: false
                }

                DltSpinEditor {
                    id: topKEditor
                    Layout.preferredWidth: 180
                    label: "TopK"
                    value: 5
                    minValue: 1
                    maxValue: 1000
                    step: 1
                }

                DltSpinEditor {
                    id: diskBatchEditor
                    Layout.fillWidth: true
                    label: "磁盘批次"
                    value: 256
                    minValue: 1
                    maxValue: 8192
                    step: 1
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
                            if (currentText === "gpu") {
                                indexStorageBox.currentIndex = 0
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
                text: dialog.dataManager && dialog.dataManager.imageSearch
                      ? dialog.dataManager.imageSearch.lastError
                      : ""
                color: "red"
                elide: Text.ElideRight
            }

            DltButton {
                text: "取消"
                onClicked: dialog.close()
            }
            DltButton {
                text: "开始搜索"
                enabled: dialog.dataManager && dialog.dataManager.imageSearch
                         && !dialog.dataManager.imageSearch.running
                onClicked: dialog.startSearch()
            }
        }
    }

    FileDialog {
        id: weightsFileDialog
        title: "选择模型权重"
        nameFilters: ["Weights (*.wts)", "All files (*)"]
        onAccepted: {
            weightsPathInput.text = Utils.getCleanPath(weightsFileDialog.file.toString())
        }
    }
}
