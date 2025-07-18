import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.project

Rectangle {
    id: control
    clip: true
    width: 160
    height: 120
    color: DltColor.Background
    border.color: DltColor.Border
    border.width: 1

    property bool selected: false
    property string name: "项目名"
    property string path: ""
    property string imagePath: ""
    property string msg: ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            color: "transparent"
            border.color: DltColor.Highlight
            border.width: selected ? 2 : 0
            Image {
                anchors.fill: parent
                anchors.margins: 2
                asynchronous: true
                fillMode: Image.PreserveAspectFit // Image.PreserveAspectCrop
                source: control.imagePath ? "file:///" + control.imagePath : ""
                sourceSize.width: parent.width
                sourceSize.height: parent.height
            }
        }
        DltText {
            Layout.leftMargin: 5
            text: control.name
        }
    }


    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onDoubleClicked: function(mouse) {
            ProjectManager.openProject(path)
        }
    }

    DltToolTip {
        text: msg
        visible: mouseArea.containsMouse
        delay: 200
        x: 0
        y: -height
    }
}
