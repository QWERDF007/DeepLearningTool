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
            onClicked: createTagDialog.openForm()
        }
    }

    DataNameFormDialog {
        id: createTagDialog
        title: "添加 Tag"
        fieldLabel: "Tag 名称"
        placeholderText: "输入 Tag 名称"
        emptyError: "请输入 Tag 名称"
        nameValidator: function(tagName) {
            if (!header.dataManager) {
                return "数据管理器不可用"
            }
            return header.dataManager.isValidTagName(tagName, -1)
        }
        onSubmitted: function(tagName) {
            if (header.dataManager) {
                header.dataManager.addTagClass(tagName)
            }
        }
    }
}
