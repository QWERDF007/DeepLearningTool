import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.project

Item {
    id: importDataForm
    width: 400
    height: 200
    property int rowH: 64
    Item {
        id: image
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
        id: label
        anchors.top: image.bottom
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
