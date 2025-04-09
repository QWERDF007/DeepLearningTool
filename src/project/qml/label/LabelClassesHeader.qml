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
            text: "标签类别:"
            font: DltFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        DltTextIconButton {
            id: addBtn
            iconSource: DltFontIcon.Add
            text: "添加标签类别"
            onClicked: {
                let pos = header.mapToItem(Qt.application.activeWindow, addBtn.x, addBtn.y)
                editor.x = pos.x + 20
                editor.y = pos.y + 20
                editor.open()
            }
        }
    }
    LabelClassEditor {
        id: editor
        onEditFinished: function (name, color, shortcut) {
            if (project) {
                project.addLabelClass(name, color, shortcut)
            }
        }
    }
}

