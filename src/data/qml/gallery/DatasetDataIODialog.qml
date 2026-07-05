import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.core
import dltool.data
import quickui

QuiPopup {
    id: dialog
    width: 600
    height: ioMode === DatasetDataIODialog.Import ? 420 : 360

    enum IOMode {
        Import,
        Export
    }

    property int ioMode: DatasetDataIODialog.Import
    property DataManager dataManager
    property var datasetIds: []
    property var datasetsModel: []
    property string datasetName: ""
    property bool waitingForClassScan: false
    property int pendingDatasetId: -1
    property int pendingDataFormat: -1
    property string pendingImageDir: ""
    property string pendingDataDir: ""

    readonly property bool importing: ioMode === DatasetDataIODialog.Import
    readonly property bool anomalyImport: importing
                                          && dataManager
                                          && dataManager.method === DeepLearningMethod.AnomalyDetection
    readonly property var dataFormatModel: dataManager
            ? (importing
               ? DataFormat.getSupportedImportDataFormat(dataManager.method)
               : DataFormat.getSupportedExportDataFormat(dataManager.method))
            : []
    readonly property string dataFormat: dataFormatBox.currentText
    readonly property bool needsDataDir: importing && dataFormat !== "Folder"
    readonly property bool canSubmit: dataManager
                                       && dataFormat.length > 0
                                       && (importing
                                           ? imagePathInput.text.trim().length > 0
                                             && datasetName.trim().length > 0
                                           : outputPathInput.text.trim().length > 0
                                             && datasetIds.length > 0)
    property int maskOutputMode: maskOutputModeBox.currentIndex < 0 ? 0 : maskOutputModeBox.currentIndex
    property var exportOptions: ({
        "mask_output_mode": maskOutputMode
    })

    function resetFormatSelection() {
        dataFormatBox.currentIndex = dataFormatModel && dataFormatModel.length > 0 ? 0 : -1
    }

    onDataFormatModelChanged: resetFormatSelection()
    Component.onCompleted: resetFormatSelection()

    function normalizedGroup(group) {
        return group === "good" || group === "良好" ? "good" : "anomaly"
    }

    function beginImport(dataFormat) {
        pendingDatasetId = dataManager.getDatasetId(datasetName)
        pendingDataFormat = dataFormat
        pendingImageDir = imagePathInput.text.trim()
        pendingDataDir = needsDataDir ? dataPathInput.text.trim() : ""

        if (anomalyImport) {
            waitingForClassScan = true
            dataManager.scanImportLabelClasses(pendingDataFormat, pendingImageDir, pendingDataDir)
            return
        }

        dataManager.importData(pendingDatasetId, pendingDataFormat, pendingImageDir, pendingDataDir)
    }

    function importWithGroups(groups) {
        if (!dataManager || pendingDatasetId < 0 || pendingDataFormat < 0) {
            return
        }
        dataManager.importDataWithLabelClassGroups(pendingDatasetId, pendingDataFormat,
                                                   pendingImageDir, pendingDataDir, groups)
    }

    Connections {
        target: dialog.dataManager
        function onImportLabelClassesScanned(success, labelClasses, message) {
            if (!dialog.waitingForClassScan) {
                return
            }
            dialog.waitingForClassScan = false
            if (!success) {
                return
            }

            let classes = labelClasses ? Array.from(labelClasses) : []
            if (classes.length === 0) {
                dialog.close()
                dialog.importWithGroups({})
            } else {
                classGroupDialog.openForClasses(classes)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 12

        QuiText {
            text: dialog.importing ? "导入数据" : "导出数据"
            font: QuiFont.Subtitle
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Item {
                visible: dialog.importing
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 64 : 0
                Layout.maximumHeight: Layout.preferredHeight

                Column {
                    anchors {
                        left: parent.left
                        leftMargin: 5
                        right: browseImageBtn.left
                        rightMargin: 5
                    }
                    height: parent.height
                    spacing: 6

                    QuiText {
                        text: "图像目录:"
                        textColor: QuiColor.FontDark
                    }
                    QuiTextField {
                        id: imagePathInput
                        width: parent.width
                        placeholderText: dialog.dataFormat === "Folder" ? "选择按类别分组的图像目录" : "选择图像目录"
                    }
                }

                QuiButton {
                    id: browseImageBtn
                    anchors {
                        bottom: parent.bottom
                        right: parent.right
                    }
                    text: "打开"
                    onClicked: imageDialog.open()
                }
            }

            Item {
                visible: dialog.needsDataDir
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 64 : 0
                Layout.maximumHeight: Layout.preferredHeight

                Column {
                    anchors {
                        left: parent.left
                        leftMargin: 5
                        right: browseDataBtn.left
                        rightMargin: 5
                    }
                    height: parent.height
                    spacing: 6

                    QuiText {
                        text: "标注目录:"
                        textColor: QuiColor.FontDark
                    }
                    QuiTextField {
                        id: dataPathInput
                        width: parent.width
                        placeholderText: "选择标注目录，可留空仅导入图像"
                    }
                }

                QuiButton {
                    id: browseDataBtn
                    anchors {
                        bottom: parent.bottom
                        right: parent.right
                    }
                    text: "打开"
                    onClicked: dataDialog.open()
                }
            }

            Item {
                visible: !dialog.importing
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 64 : 0
                Layout.maximumHeight: Layout.preferredHeight

                Column {
                    anchors {
                        left: parent.left
                        leftMargin: 5
                        right: browseOutputBtn.left
                        rightMargin: 5
                    }
                    height: parent.height
                    spacing: 6

                    QuiText {
                        text: "输出目录:"
                        textColor: QuiColor.FontDark
                    }
                    QuiTextField {
                        id: outputPathInput
                        width: parent.width
                        placeholderText: "选择导出目录"
                    }
                }

                QuiButton {
                    id: browseOutputBtn
                    anchors {
                        bottom: parent.bottom
                        right: parent.right
                    }
                    text: "打开"
                    onClicked: outputDialog.open()
                }
            }

            Row {
                Layout.fillWidth: true
                Layout.preferredHeight: 64

                Column {
                    spacing: 10
                    width: parent.width / 2
                    height: parent.height
                    QuiText {
                        text: "数据格式:"
                        textColor: QuiColor.FontDark
                    }
                    QuiComboBox {
                        id: dataFormatBox
                        width: 240
                        model: dialog.dataFormatModel
                    }
                }

                Column {
                    spacing: 10
                    width: parent.width / 2
                    height: parent.height
                    QuiText {
                        text: "数据集:"
                        textColor: QuiColor.FontDark
                    }
                    QuiComboBox {
                        visible: dialog.importing
                        id: datasetBox
                        width: 240
                        model: dialog.datasetsModel
                        currentIndex: indexOfValue(dialog.datasetName)
                        onActivated: dialog.datasetName = currentText
                    }
                    QuiText {
                        visible: !dialog.importing
                        width: 240
                        text: dialog.datasetName
                        elide: Text.ElideRight
                    }
                }
            }

            Row {
                visible: !dialog.importing && dialog.dataFormat === "Mask"
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 64 : 0
                Layout.maximumHeight: Layout.preferredHeight

                Column {
                    spacing: 10
                    width: parent.width / 2
                    height: parent.height
                    QuiText {
                        text: "Mask导出格式:"
                        textColor: QuiColor.FontDark
                    }
                    QuiComboBox {
                        id: maskOutputModeBox
                        width: 240
                        model: ["全255", "按类别 + 1"]
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            height: 32

            QuiButton {
                anchors.right: submitBtn.left
                anchors.rightMargin: 5
                width: parent.width / 4
                height: parent.height
                text: "取消"
                onClicked: dialog.close()
            }

            QuiButton {
                id: submitBtn
                enabled: dialog.canSubmit
                anchors.right: parent.right
                width: parent.width / 4
                height: parent.height
                text: dialog.importing ? "导入" : "导出"
                normalColor: QuiColor.Highlight
                onClicked: {
                    let data_format = DataFormat.getDataFormat(dialog.dataFormat)
                    if (dialog.importing) {
                        if (!dialog.anomalyImport) {
                            dialog.close()
                        }
                        dialog.beginImport(data_format)
                    } else {
                        dialog.close()
                        dialog.dataManager.exportDatasets(dialog.datasetIds, data_format,
                                                          outputPathInput.text.trim(),
                                                          dialog.exportOptions)
                    }
                }
            }
        }
    }

    FolderDialog {
        id: imageDialog
        folder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        onAccepted: imagePathInput.text = Utils.getCleanPath(imageDialog.folder.toString())
    }

    FolderDialog {
        id: dataDialog
        folder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: dataPathInput.text = Utils.getCleanPath(dataDialog.folder.toString())
    }

    FolderDialog {
        id: outputDialog
        folder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: outputPathInput.text = Utils.getCleanPath(outputDialog.folder.toString())
    }

    QuiPopup {
        id: classGroupDialog
        width: 520
        height: 420
        maskOpacity: 0.2

        property var pendingClasses: []

        function openForClasses(labelClasses) {
            pendingClasses = labelClasses ? Array.from(labelClasses) : []
            if (pendingClasses.length > 0) {
                open()
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            QuiText {
                text: "设置类别分组"
                font: QuiFont.Subtitle
            }

            ListView {
                id: classGroupList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: classGroupDialog.pendingClasses
                ScrollBar.vertical: QuiScrollBar {}

                delegate: Rectangle {
                    width: classGroupList.width
                    height: 40
                    radius: 3
                    color: Qt.lighter(QuiColor.Primary, 1.1)
                    border.width: 1
                    border.color: QuiColor.Border

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            radius: 3
                            color: modelData.color || "#cc3333"
                            border.width: 1
                            border.color: "#222222"
                        }

                        QuiText {
                            Layout.fillWidth: true
                            text: modelData.name || ""
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        QuiComboBox {
                            Layout.preferredWidth: 120
                            model: ["异常", "良好"]
                            currentIndex: dialog.normalizedGroup(modelData.group) === "good" ? 1 : 0
                            onActivated: function(idx) {
                                classGroupDialog.pendingClasses[index].group = idx === 1 ? "good" : "anomaly"
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item {
                    Layout.fillWidth: true
                }

                QuiButton {
                    text: "取消"
                    onClicked: classGroupDialog.close()
                }

                QuiButton {
                    text: "导入"
                    normalColor: QuiColor.Highlight
                    onClicked: {
                        let groups = ({})
                        for (let i = 0; i < classGroupDialog.pendingClasses.length; ++i) {
                            let item = classGroupDialog.pendingClasses[i]
                            let name = String(item.name ?? "")
                            if (name.length > 0) {
                                groups[name] = dialog.normalizedGroup(item.group)
                            }
                        }
                        classGroupDialog.close()
                        dialog.close()
                        dialog.importWithGroups(groups)
                    }
                }
            }
        }
    }
}
