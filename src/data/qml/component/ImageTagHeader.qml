import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Item {
    id: header
    property DataManager dataManager
    readonly property bool editorOpen: createTagDialog.visible
    RowLayout {
        anchors.fill: parent
        QuiText {
            text: "Tag:"
            font: QuiFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        QuiTextIconButton {
            id: addBtn
            iconSource: QuiFontIcon.Add
            text: "添加Tag"
            onClicked: createTagDialog.openForCreate()
        }
    }

    ImageTagFormDialog {
        id: createTagDialog
        dataManager: header.dataManager
        onSubmitted: function(tagId, tagName, shortcut) {
            if (header.dataManager) {
                header.dataManager.addTagClass(tagName, shortcut)
            }
        }
    }
}
