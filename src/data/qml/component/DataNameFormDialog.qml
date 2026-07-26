import QtQuick
import QtQuick.Layouts

import dltool.ui
import quickui

DataFormDialog {
    id: control

    property alias text: nameField.text
    property string fieldLabel: "名称"
    property string placeholderText: "输入名称"
    property string emptyError: "请输入名称"
    property var nameValidator: null
    property string validationError: ""

    errorText: validationError
    submitEnabled: nameField.text.trim().length > 0 && validationError.length === 0

    signal submitted(string name)

    function validateName() {
        const name = nameField.text.trim()
        if (name.length === 0) {
            validationError = emptyError
            return false
        }

        validationError = nameValidator ? nameValidator(name) : ""
        return validationError.length === 0
    }

    function openForm(initialName) {
        nameField.text = initialName === undefined ? "" : initialName
        validateName()
        open()
        nameField.forceActiveFocus()
    }

    onAccepted: {
        if (!validateName()) {
            return
        }

        const name = nameField.text.trim()
        close()
        submitted(name)
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6

        QuiText {
            text: control.fieldLabel
            color: QuiColor.FontDark
        }

        QuiTextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: control.placeholderText
            onTextChanged: control.validateName()
        }
    }
}
