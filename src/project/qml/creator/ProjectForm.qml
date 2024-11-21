import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.project

Item {
    id: projectForm
    // clip: true
    width: 400
    height: 200
    property int rowH: 64
    property alias name: nameInput.text
    property alias path: pathInput.text
    property alias description: descInput.text
    property alias image_base_path: imageBasePathInput.text
    property alias msg: msgInput.text
    property int method: -1
    property bool isValid: msg === ""
    property string folder: Utils.documentsLocation()

    onFolderChanged: {
        pathInput.text = projectForm.folder + "/" + nameInput.text + ProjectManager.projectSuffix()
    }

    function reset() {
        projectForm.folder = Utils.documentsLocation()
        projectForm.name = "新项目"
        projectForm.description = ""
        projectForm.image_base_path = ""
    }

    Item {
        anchors.fill: parent
        Item {
            id: basic
            width: parent.width
            height: rowH
            Column {
                id: nameCol
                anchors.left: parent.left
                height: parent.height
                width: parent.width / 4
                DltText {
                    text: "项目名称:"
                    textColor: DltColor.FontDark
                }
                DltTextField {
                    id: nameInput
                    width: parent.width
                    text: "新项目"
                    onTextChanged: {
                        pathInput.text = projectForm.folder + "/" + nameInput.text + ProjectManager.projectSuffix()
                    }
                }
            }
            Column {
                anchors {
                    left: nameCol.right
                    leftMargin: 5
                    right: browseBtn.left
                    rightMargin: 5
                }
                height: parent.height
                DltText {
                    text: "项目路径:"
                    textColor: DltColor.FontDark
                }
                DltTextField {
                    id: pathInput
                    width: parent.width
                    // text: folder + "/" + nameInput.text + ProjectManager.projectSuffix()
                }
            }
            DltButton {
                id: browseBtn
                anchors {
                    bottom: parent.bottom
                    right: parent.right
                }
                text: "浏览"
                onClicked: {
                    folderDialog.open()
                }
            }
        }

        Item {
            id: desc
            anchors.top: basic.bottom
            anchors.topMargin: 10
            width: parent.width
            height: rowH
            Column {
                anchors.fill: parent
                DltText {
                    text: "项目描述:"
                    textColor: DltColor.FontDark
                }
                DltTextField {
                    id: descInput
                    width: parent.width
                    placeholderText: "输入项目描述"
                }
            }
        }

        Item {
            id: imageBasePath
            anchors.top: desc.bottom
            anchors.topMargin: 10
            width: parent.width
            height: rowH
            Column {
                anchors.fill: parent
                Row{
                    DltText {
                        text: "图像基准路径:"
                        textColor: DltColor.FontDark
                    }
                }
                DltTextField {
                    id: imageBasePathInput
                    width: parent.width
                    placeholderText: "输入图像基准路径"
                }
            }
        }
        Item {
            anchors{
                top: imageBasePath.bottom
                topMargin: 10
                left: parent.left
                leftMargin: 10
                right: parent.right
                rightMargin: 10
            }
            DltText {
                id: msgInput
                text: ProjectManager.isProjectValid(method, path, true)
                color: "#F9B900"
            }
        }
    }

    FolderDialog {
        id: folderDialog
        folder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            projectForm.folder = folderDialog.folder.toString().slice(8)
        }
    }
}
