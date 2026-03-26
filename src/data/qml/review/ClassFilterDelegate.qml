import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data

Rectangle {
    id: control
    width: parent ? parent.width : 200
    height: 32
    color: backgroundColor
    
    property string className: ""
    property string classShortcut: ""
    property color classColor: "black"
    property color backgroundColor: Qt.lighter(DltColor.Primary, 1.2)
    property int classId: -1
    property int ordinalIndex: -1
    property var listView
    property LabelClassesModel labelClasses

    signal editClicked
    signal deleteClicked
    signal clicked(int classId)

    // 计算对比色：根据背景色亮度决定使用黑色或白色文本
    // 使用相对亮度公式: L = 0.299*R + 0.587*G + 0.114*B
    function getContrastColor(bgColor) {
        var r = bgColor.r
        var g = bgColor.g
        var b = bgColor.b
        var luminance = 0.299 * r + 0.587 * g + 0.114 * b
        return luminance > 0.5 ? "black" : "white"
    }

    MouseArea {
        anchors.fill: parent
        onClicked: control.clicked(control.classId)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 5
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: height
            radius: 3
            color: control.classColor
            border.width: 1
            border.color: "black"
            DltText {
                text: control.classShortcut
                color: control.getContrastColor(control.classColor)
                anchors.centerIn: parent
            }
        }
        DltText {
            text: control.className
            Layout.fillWidth: true
        }
    }
    
    // 按钮放在最上层，可以接收点击事件
    RowLayout {
        z: 1
        anchors {
            top: parent.top
            bottom: parent.bottom
            right: parent.right
            rightMargin: 5
        }
        spacing: 3
        DltTextIconButton {
            iconSource: DltFontIcon.Edit
            onClicked: control.editClicked()
            normalColor: control.backgroundColor
        }
        DltTextIconButton {
            iconSource: DltFontIcon.Delete
            onClicked: control.deleteClicked()
            normalColor: control.backgroundColor
        }
    }
}
