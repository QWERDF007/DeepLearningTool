import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    id: projectNameType
    width: 200
    height: 200
    color: DltColor.Primary

    property string method: ""
    property string name: ""
    signal projectNameChanged(string name)
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        DltText {
            text: projectNameType.name
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
            text: projectNameType.method
            Layout.fillWidth: true
            font: DltFont.Subtitle
        }
    }
    DltTextIconButton {
        id: editBtn
        anchors{
            top: parent.top
            right: parent.right
            margins: 5
        }
        visible: projectNameType.name !== ""
        color: hovered ? DltColor.Button : "transparent"
        iconColor: DltColor.FontPrimary
        iconSource: DltFontIcon.Edit
        text: "编辑项目名称"
        onClicked: {
            editor.text = projectNameType.name
            let pos = mapToItem(null, 0, 0)
            editor.x = pos.x + 20
            editor.y = pos.y + 20
            editor.open()
        }
    }

    DltEditor {
        id: editor
        description: editBtn.text
        onEditTextChanged: function(newName) {
            projectNameType.projectNameChanged(newName)
        }
    }
}
