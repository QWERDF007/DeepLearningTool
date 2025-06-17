import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.data
import dltool.project

Item {
    id: importDataForm
    width: 400
    height: 200
    property int rowH: 64
    // property Project project: ProjectManager.currentProject
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
            DltText {
                text: "图像目录:"
                textColor: DltColor.FontDark
            }
            DltTextField {
                id: imagePathInput
                width: parent.width
                placeholderText: "输入图像目录"
            }
        }
        DltButton {
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
            DltText {
                text: "标注目录:"
                textColor: DltColor.FontDark
            }
            DltTextField {
                id: labelPathInput
                width: parent.width
                placeholderText: "输入标注目录"
            }
        }
        DltButton {
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
            DltText {
                text: "数据格式:"
                textColor: DltColor.FontDark
            }
            DltComboBox {
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
            DltText {
                text: "数据集:"
                textColor: DltColor.FontDark
            }
            DltComboBox {
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
            imagePathInput.text = imageDialog.folder.toString().slice(8)
        }
    }

    FolderDialog {
        id: labelDialog
        folder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            labelPathInput.text = labelDialog.folder.toString().slice(8)
        }
    }
}
