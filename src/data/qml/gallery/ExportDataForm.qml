import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.data

Item {
    id: exportDataForm
    width: 400
    height: 180

    property int rowH: 64
    property string datasetName: ""
    property alias dataFormatModel: dataFormatBox.model
    property string dataFormat: dataFormatBox.currentText
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
            DltText {
                text: "输出目录:"
                textColor: DltColor.FontDark
            }
            DltTextField {
                id: outputPathInput
                width: parent.width
                placeholderText: "选择导出目录"
            }
        }
        DltButton {
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
        anchors.top: outputForm.bottom
        anchors.topMargin: 20
        width: parent.width
        height: rowH
        Column {
            spacing: 10
            width: parent.width / 2
            height: parent.height
            DltText {
                text: "数据格式:"
                textColor: DltColor.FontDark
            }
            DltComboBox {
                id: dataFormatBox
                width: 240
            }
        }

        Column {
            spacing: 10
            width: parent.width / 2
            height: parent.height
            DltText {
                text: "数据集:"
            }
            DltText {
                width: 240
                text: datasetName
                elide: Text.ElideRight
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
