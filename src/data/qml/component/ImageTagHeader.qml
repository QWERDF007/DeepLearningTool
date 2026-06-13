import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Item {
    id: header
    property DataManager dataManager
    RowLayout {
        anchors.fill: parent
        QuiText {
            text: "图像Tag:"
            font: QuiFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        QuiTextIconButton {
            id: addBtn
            iconSource: QuiFontIcon.Add
            text: "添加Tag"
            onClicked: {
                editor.text = ""
                let pos = mapToItem(null, 0, 0) // 获取当前item左上角的全局坐标
                editor.x = pos.x + 20
                editor.y = pos.y + 20
                editor.open()
            }
        }
    }
    QuiEditor {
        id: editor
        description: "输入Tag名称"
        onEditTextChanged: function (tagName) {
            if (dataManager) {
                dataManager.addTagClass(tagName)
            }
        }
    }
}
