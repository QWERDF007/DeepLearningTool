import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
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
                text: description
                wrapMode: Text.WrapAnywhere
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
                text: path
                wrapMode: Text.WrapAnywhere
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
                text: image_base_path
                wrapMode: Text.WrapAnywhere
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
                text: label_classes
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
                text: label_instances_images
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
                text: ctime + " " + mtime
                wrapMode: Text.WrapAnywhere
            }
        }
        Item {
            Layout.fillHeight: true
        }
    }

    DltTextIconButton {
        anchors{
            top: parent.top
            right: parent.right
            margins: 5
        }
        visible: path !== ""
        color: hovered ? DltColor.Button : "transparent"
        iconColor: DltColor.FontPrimary
        iconSource: DltFontIcon.Edit
        text: "编辑项目描述"
        onClicked: {
            console.log("编辑项目描述...")
        }
    }
}
