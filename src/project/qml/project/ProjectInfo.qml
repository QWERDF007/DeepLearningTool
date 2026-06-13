import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

Rectangle {
    id: projectInfo
    clip: true
    height: 200
    width: 200
    color: QuiColor.Primary
    property string path: ""
    property string description: ""
    property string image_base_path: ""
    property string label_classes: ""
    property string label_instances_images: ""
    property string ctime: ""
    property string mtime: ""

    signal projectDescriptionChanged(string desc)

    QuiMenu {
        id: menu
        width: 200
        QuiMenuItem {
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
            QuiText {
                Layout.topMargin: 5
                text: "项目描述:"
                textColor: QuiColor.FontDark
            }
            QuiText {
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
            QuiText {
                text: "项目文件:"
                textColor: QuiColor.FontDark
            }
            QuiText {
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
            QuiText {
                text: "图像基准路径:"
                textColor: QuiColor.FontDark
            }
            QuiText {
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
            QuiText {
                text: "标签类别:"
                textColor: QuiColor.FontDark
            }
            QuiText {
                Layout.fillWidth: true
                text: projectInfo.label_classes
                wrapMode: Text.WrapAnywhere
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            QuiText {
                text: "标注实例 / 图像:"
                textColor: QuiColor.FontDark
            }
            QuiText {
                Layout.fillWidth: true
                text: projectInfo.label_instances_images
                wrapMode: Text.WrapAnywhere
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            QuiText {
                text: "创建时间 / 修改时间:"
                textColor: QuiColor.FontDark
            }
            QuiText {
                Layout.fillWidth: true
                text: projectInfo.ctime + " " + projectInfo.mtime
                wrapMode: Text.WrapAnywhere
            }
        }
        Item {
            Layout.fillHeight: true
        }
    }

    QuiTextIconButton {
        id: editBtn
        anchors{
            top: parent.top
            right: parent.right
            margins: 5
        }
        visible: projectInfo.path !== ""
        color: hovered ? QuiColor.Button : QuiColor.Transparent
        iconColor: QuiColor.FontPrimary
        iconSource: QuiFontIcon.Edit
        text: "编辑项目描述"
        onClicked: {
            editor.text = projectInfo.description
            let pos = mapToItem(null, 0, 0)
            editor.x = pos.x + 20
            editor.y = pos.y + 20
            editor.open()
        }
    }

    QuiEditor {
        id: editor
        description: editBtn.text
        onEditTextChanged: function (newDescription) {
            projectInfo.projectDescriptionChanged(newDescription)
        }
    }
}
