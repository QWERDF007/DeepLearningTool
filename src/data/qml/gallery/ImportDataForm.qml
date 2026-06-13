import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.data
import quickui

Item {
    id: importDataForm
    width: 400
    height: 200
    property int rowH: 64
    property string datasetName: ""
    property alias datasetsModel: datasetBox.model
    property alias dataFormatModel: dataFormatBox.model
    property string dataFormat: dataFormatBox.currentText
    property string image_dir: imagePathInput.text
    property string data_dir: labelPathInput.text

    Item {
        id: imageForm
        width: parent.width
        height: rowH
        Column {
            anchors {
                left: parent.left
                leftMargin: 5
                right: browseImageBtn.left
                rightMargin: 5
            }
            height: parent.height
            QuiText {
                text: "图像目录:"
                textColor: QuiColor.FontDark
            }
            QuiTextField {
                id: imagePathInput
                width: parent.width
                placeholderText: "输入图像目录"
            }
        }
        QuiButton {
            id: browseImageBtn
            anchors {
                bottom: parent.bottom
                right: parent.right
            }
            text: "打开"
            onClicked: {
                imageDialog.open()
            }
        }
    }
    Item {
        id: labelForm
        anchors.top: imageForm.bottom
        anchors.topMargin: 10
        width: parent.width
        height: rowH
        Column {
            anchors {
                left: parent.left
                leftMargin: 5
                right: browseLabelBtn.left
                rightMargin: 5
            }
            height: parent.height
            QuiText {
                text: "标注目录:"
                textColor: QuiColor.FontDark
            }
            QuiTextField {
                id: labelPathInput
                width: parent.width
                placeholderText: "输入标注目录"
            }
        }
        QuiButton {
            id: browseLabelBtn
            anchors {
                bottom: parent.bottom
                right: parent.right
            }
            text: "打开"
            onClicked: {
                labelDialog.open()
            }
        }
    }
    Row {
        anchors.top: labelForm.bottom
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
                // model: DataFormat.getSupportedDataFormat()
                // Component.onCompleted: {
                //     console.log(DataFormat.getSupportedDataFormat())
                //     model = DataFormat.getSupportedDataFormat()
                // }
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
                id: datasetBox
                width: 240
                currentIndex: indexOfValue(datasetName)
                onActivated: {
                    datasetName = currentText
                }
            }
        }
    }

    FolderDialog {
        id: imageDialog
        folder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        onAccepted: {
            imagePathInput.text = Utils.getCleanPath(imageDialog.folder.toString())
        }
    }

    FolderDialog {
        id: labelDialog
        folder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            labelPathInput.text = Utils.getCleanPath(labelDialog.folder.toString())
        }
    }
}
