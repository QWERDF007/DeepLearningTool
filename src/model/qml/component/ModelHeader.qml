import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Item {
    id: header
    property string text
    property bool addEnable: true

    RowLayout {
        anchors.fill: parent
        DltText {
            text: header.text
            font: DltFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        DltTextIconButton {
            id: addBtn
            visible: addEnable
            iconSource: DltFontIcon.Add
            text: "添加模型"
            onClicked: {
                editor.text = ""
                let pos = mapToItem(null, 0, 0)
                editor.x = pos.x + 20
                editor.y = pos.y + 20
                editor.open()
            }
        }
    }
    DltEditor {
        id: editor
        description: "输入模型名称"
        onEditTextChanged: function (modelName) {
            
        }
    }
}
