import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data

Item {
    id: header
    property DataManager dataManager
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
                let pos = mapToItem(null, 0, 0)
                editor.x = pos.x + 20
                editor.y = pos.y + 20
                editor.open()
            }
        }
    }
    LabelClassEditor {
        id: editor
        isCreate: true
        onLabelClassChanged: function (classId, className, classColor, classShortcut, ordinalIndex) {
            if (labelClasses) {
                editor.msg = labelClasses.isValid(classId, className, classShortcut, -1)
            }
        }
        onLabelClassChangedAccepted: function (classId, className, classColor, classShortcut, ordinalIndex) {
            if (dataManager) {
                dataManager.addLabelClass(className, classColor, classShortcut)
            }
        }
    }
}

