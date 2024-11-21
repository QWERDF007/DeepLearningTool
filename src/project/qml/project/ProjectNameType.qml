import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    width: 200
    height: 200
    color: DltColor.Primary

    property string method: ""
    property string name: ""
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        DltText {
            text: name
            Layout.topMargin: 5
            Layout.fillWidth: true
            font: DltFont.Subtitle
        }
        Rectangle {
            Layout.margins: 5
            Layout.fillWidth: true
            height: 2
            color: DltColor.Background
        }
        DltText {
            text: method
            Layout.fillWidth: true
            font: DltFont.Subtitle
        }
    }
    DltTextIconButton {
        anchors{
            top: parent.top
            right: parent.right
            margins: 5
        }
        visible: name !== ""
        color: hovered ? DltColor.Button : "transparent"
        iconColor: DltColor.FontPrimary
        iconSource: DltFontIcon.Edit
        text: "编辑项目名"
        onClicked: {
            console.log("编辑项目名...")
        }
    }
}
