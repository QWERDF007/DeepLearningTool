import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.project
import quickui

Rectangle {
    id: control
    clip: true
    width: 160
    height: 120
    color: QuiColor.Background

    property bool selected: false
    property string name: "项目名"
    property string path: ""
    property string imagePath: ""
    property string msg: ""

    border.color: selected ? QuiColor.Highlight : QuiColor.Border
    border.width: 2

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 2
        Image {
            Layout.fillHeight: true
            Layout.fillWidth: true
            asynchronous: true
            fillMode: Image.PreserveAspectFit // Image.PreserveAspectCrop
            source: control.imagePath ? Utils.toFileUrl(control.imagePath) : ""
            sourceSize.width: parent.width
            sourceSize.height: parent.height
        }
        QuiText {
            Layout.fillWidth: true
            Layout.margins: 5
            verticalAlignment: Text.AlignVCenter
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

    QuiToolTip {
        text: msg
        visible: mouseArea.containsMouse
        delay: 200
        x: 0
        y: -height
    }
}
