import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

Rectangle {
    id: projectNameType
    width: 200
    height: 200
    color: QuiColor.Primary

    property string method: ""
    property string name: ""
    signal projectNameChanged(string name)
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        QuiText {
            text: projectNameType.name
            Layout.topMargin: 5
            Layout.fillWidth: true
            font: QuiFont.Subtitle
        }
        Rectangle {
            Layout.margins: 5
            Layout.fillWidth: true
            height: 2
            color: QuiColor.Background
        }
        QuiText {
            text: projectNameType.method
            Layout.fillWidth: true
            font: QuiFont.Subtitle
        }
    }
    QuiTextIconButton {
        id: editBtn
        anchors{
            top: parent.top
            right: parent.right
            margins: 5
        }
        visible: projectNameType.name !== ""
        color: hovered ? QuiColor.Button : QuiColor.Transparent
        iconColor: QuiColor.FontPrimary
        iconSource: QuiFontIcon.Edit
        text: "编辑项目名称"
        onClicked: {
            editor.text = projectNameType.name
            let pos = mapToItem(null, 0, 0)
            editor.x = pos.x + 20
            editor.y = pos.y + 20
            editor.open()
        }
    }

    QuiEditor {
        id: editor
        description: editBtn.text
        onEditTextChanged: function(newName) {
            projectNameType.projectNameChanged(newName)
        }
    }
}
