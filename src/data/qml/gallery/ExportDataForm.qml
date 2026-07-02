import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.data
import quickui

Item {
    id: exportDataForm
    width: 400
    height: 240

    property int rowH: 64
    property string datasetName: ""
    property alias dataFormatModel: dataFormatBox.model
    property string dataFormat: dataFormatBox.currentText
    property int maskOutputMode: maskOutputModeBox.currentIndex < 0 ? 0 : maskOutputModeBox.currentIndex
    property var exportOptions: ({
        "mask_output_mode": maskOutputMode
    })
    property string output_dir: outputPathInput.text

    Item {
        id: outputForm
        width: parent.width
        height: rowH
        Column {
            anchors {
                left: parent.left
                leftMargin: 5
                right: browseOutputBtn.left
                rightMargin: 5
            }
            height: parent.height
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
            onClicked: {
                outputDialog.open()
            }
        }
    }

    Row {
        id: formatRow
        anchors.top: outputForm.bottom
        anchors.topMargin: 20
        width: parent.width
        height: rowH
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
            }
        }

        Column {
            spacing: 10
            width: parent.width / 2
            height: parent.height
            QuiText {
                text: "数据集:"
            }
            QuiText {
                width: 240
                text: datasetName
                elide: Text.ElideRight
            }
        }
    }

    Row {
        id: maskOptionsRow
        visible: dataFormat === "Mask"
        anchors.top: formatRow.bottom
        anchors.topMargin: 12
        width: parent.width
        height: rowH
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

    FolderDialog {
        id: outputDialog
        folder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            outputPathInput.text = Utils.getCleanPath(outputDialog.folder.toString())
        }
    }
}
