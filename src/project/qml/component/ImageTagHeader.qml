import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Item {
    id: header
    property Project project
    RowLayout {
        anchors.fill: parent
        DltText {
            text: "图像Tag:"
            font: DltFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        DltTextIconButton {
            id: addBtn
            iconSource: DltFontIcon.Add
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
    DltEditor {
        id: editor
        description: "输入Tag名称"
        onEditTextChanged: function (tagName) {
            if (project) {
                project.addTagClass(tagName)
            }
        }
    }
}
