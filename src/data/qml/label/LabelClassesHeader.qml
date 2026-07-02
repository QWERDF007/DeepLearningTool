import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Item {
    id: header
    property DataManager dataManager
    property LabelClassesModel labelClasses: null

    function usedColors() {
        let used = []
        if (!labelClasses) {
            return used
        }

        for (let row = 0; row < labelClasses.rowCount(); ++row) {
            let modelIndex = labelClasses.index(row, 0)
            let color = labelClasses.data(modelIndex, LabelClassesModel.ColorRole)
            if (color) {
                used.push(color)
            }
        }
        return used
    }

    function nextRecommendedColor() {
        return Utils.nextRecommendedColor(usedColors())
    }

    RowLayout {
        anchors.fill: parent
        QuiText {
            text: "标签类别:"
            font: QuiFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        QuiTextIconButton {
            id: addBtn
            iconSource: QuiFontIcon.Add
            text: "添加标签类别"
            onClicked: {
                let pos = mapToItem(null, 0, 0)
                editor.x = pos.x + 20
                editor.y = pos.y + 20
                editor.openForCreate(nextRecommendedColor())
            }
        }
    }
    LabelClassEditor {
        id: editor
        isCreate: true
        onLabelClassChanged: function (classId, className, classColor, classShortcut, ordinalIndex) {
            if (dataManager) {
                let nameMsg = dataManager.isValidClassName(className, classId)
                if (nameMsg.length > 0) {
                    editor.msg = nameMsg
                    return
                }
            }
            if (labelClasses) {
                editor.msg = labelClasses.isValid(classId, className, classColor, classShortcut, -1)
            }
        }
        onLabelClassChangedAccepted: function (classId, className, classColor, classShortcut, ordinalIndex) {
            if (dataManager
                    && dataManager.isValidClassName(className, classId).length === 0
                    && (!labelClasses || !labelClasses.isValid(classId, className, classColor, classShortcut, -1).startsWith("error:"))) {
                dataManager.addLabelClass(className, classColor, classShortcut)
            }
        }
    }
}

