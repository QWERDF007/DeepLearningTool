import QtQuick
import QtQuick.Layouts

import dltool.data
import dltool.ui
import quickui

DataFormDialog {
    id: control
    width: 480
    title: isCreate ? "添加 Tag" : "修改 Tag"
    errorText: validationError
    submitEnabled: validationError.length === 0 && nameField.text.trim().length > 0

    property DataManager dataManager
    property int tagId: -1
    property bool isCreate: true
    property string validationError: ""

    signal submitted(int tagId, string name, string shortcut)

    function validateInput() {
        const name = nameField.text.trim()
        if (name.length === 0) {
            validationError = "error:请输入 Tag 名称"
            return false
        }
        if (!dataManager) {
            validationError = "error:数据管理器不可用"
            return false
        }
        validationError = dataManager.isValidTag(name, shortcutField.text, tagId)
        return validationError.length === 0
    }

    function openForCreate() {
        tagId = -1
        isCreate = true
        nameField.text = ""
        shortcutField.text = ""
        validateInput()
        open()
        nameField.forceActiveFocus()
    }

    function openForEdit(id, name, shortcut) {
        tagId = id
        isCreate = false
        nameField.text = name || ""
        shortcutField.text = shortcut || ""
        validateInput()
        open()
        nameField.forceActiveFocus()
    }

    onAccepted: {
        if (!validateInput()) {
            return
        }
        const normalizedName = nameField.text.trim()
        const normalizedShortcut = shortcutField.text.trim()
        close()
        submitted(tagId, normalizedName, normalizedShortcut)
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6

        QuiText { text: "Tag 名称"; color: QuiColor.FontDark }
        QuiTextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: "输入 Tag 名称"
            onTextChanged: control.validateInput()
        }

        QuiText { text: "快捷键"; color: QuiColor.FontDark }
        QuiTextField {
            id: shortcutField
            Layout.fillWidth: true
            placeholderText: "输入单个快捷键（留空则取消）"
            onTextChanged: control.validateInput()
        }
    }
}
