import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    id: projectInfo
    clip: true
    height: 200
    width: 200
    color: DltColor.Primary
    property string path: ""
    property string description: ""
    property string image_base_path: ""
    property string label_classes: ""
    property string label_instances_images: ""
    property string ctime: ""
    property string mtime: ""

    signal projectDescriptionChanged(string desc)

    DltMenu {
        id: menu
        width: 200
        DltMenuItem {
            text: "复制"
            onTriggered: {
                copyboard.selectAll()
                copyboard.copy()
            }
        }
    }
    TextEdit {
        id: copyboard
        visible: false
    }

    

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 10
        ColumnLayout {
            Layout.fillWidth: true
            DltText {
                Layout.topMargin: 5
                text: "项目描述:"
                textColor: DltColor.FontDark
            }
            DltText {
                Layout.fillWidth: true
                text: projectInfo.description
                wrapMode: Text.WrapAnywhere
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        copyboard.text = projectInfo.description
                        menu.popup()
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            DltText {
                text: "项目文件:"
                textColor: DltColor.FontDark
            }
            DltText {
                Layout.fillWidth: true
                text: projectInfo.path
                wrapMode: Text.WrapAnywhere
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        copyboard.text = projectInfo.path
                        menu.popup()
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            DltText {
                text: "图像基准路径:"
                textColor: DltColor.FontDark
            }
            DltText {
                Layout.fillWidth: true
                text: projectInfo.image_base_path
                wrapMode: Text.WrapAnywhere
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        copyboard.text = projectInfo.image_base_path
                        menu.popup()
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            DltText {
                text: "标签类别:"
                textColor: DltColor.FontDark
            }
            DltText {
                Layout.fillWidth: true
                text: projectInfo.label_classes
                wrapMode: Text.WrapAnywhere
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            DltText {
                text: "标注实例 / 图像:"
                textColor: DltColor.FontDark
            }
            DltText {
                Layout.fillWidth: true
                text: projectInfo.label_instances_images
                wrapMode: Text.WrapAnywhere
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            DltText {
                text: "创建时间 / 修改时间:"
                textColor: DltColor.FontDark
            }
            DltText {
                Layout.fillWidth: true
                text: projectInfo.ctime + " " + projectInfo.mtime
                wrapMode: Text.WrapAnywhere
            }
        }
        Item {
            Layout.fillHeight: true
        }
    }

    DltTextIconButton {
        id: editBtn
        anchors{
            top: parent.top
            right: parent.right
            margins: 5
        }
        visible: projectInfo.path !== ""
        color: hovered ? DltColor.Button : DltColor.Transparent
        iconColor: DltColor.FontPrimary
        iconSource: DltFontIcon.Edit
        text: "编辑项目描述"
        onClicked: {
            editor.text = projectInfo.description
            let pos = mapToItem(null, 0, 0)
            editor.x = pos.x + 20
            editor.y = pos.y + 20
            editor.open()
        }
    }

    DltEditor {
        id: editor
        description: editBtn.text
        onEditTextChanged: function (newDescription) {
            projectInfo.projectDescriptionChanged(newDescription)
        }
    }
}
