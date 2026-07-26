import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import quickui
import "../component"

DataFormDialog {
    id: control

    width: 600
    title: isCreate ? "创建标签类别" : "修改标签类别"
    errorText: msg
    errorColor: getMsgColor()
    submitEnabled: !isError()

    default property alias extraFields: extraFieldsColumn.data

    property int classId: -1
    property alias className: nameField.text
    property alias classColor: colorField.text
    property alias classShortcut: shortcutField.text
    property alias ordinalIndex: ordinalField.text
    property string classGroup: "anomaly"
    property string defaultClassGroup: "anomaly"
    property string msg: ""
    property int maxOrdinalIndex: 0
    property int extraFieldsHeight: 0
    property bool isCreate: true

    signal labelClassChanged(int classId, string className, string classColor, string classShortcut, int ordinalIndex,
                             string classGroup)
    signal labelClassChangedAccepted(int classId, string className, string classColor, string classShortcut,
                                     int ordinalIndex, string classGroup)

    function isError() {
        return msg.startsWith("error:")
    }

    function getMsgColor() {
        return msg.startsWith("warning:") ? "orange" : "#D83B01"
    }

    function currentOrdinalIndex() {
        if (isCreate) {
            return -1
        }

        const ordinalText = ordinalField.text.trim()
        return /^\d+$/.test(ordinalText) ? Number(ordinalText) : -1
    }

    function notifyChanged() {
        labelClassChanged(classId, className, classColor, classShortcut,
                          currentOrdinalIndex(), classGroup)
    }

    function normalizeClassGroup(group) {
        return group
    }

    function clearInput(defaultColor, defaultGroup) {
        nameField.text = ""
        colorField.text = defaultColor !== undefined ? defaultColor : "#000000"
        shortcutField.text = ""
        ordinalField.text = ""
        classGroup = defaultGroup !== undefined ? normalizeClassGroup(defaultGroup) : defaultClassGroup
        msg = ""
    }

    function openForCreate(defaultColor, defaultGroup) {
        clearInput(defaultColor, defaultGroup)
        classId = -1
        isCreate = true
        colorDialog.currentColor = colorField.text
        open()
        nameField.forceActiveFocus()
        notifyChanged()
    }

    function openForEdit(id, name, color, shortcut, ordinal, group) {
        classId = id
        nameField.text = name
        colorField.text = color
        shortcutField.text = shortcut
        ordinalField.text = ordinal
        classGroup = normalizeClassGroup(group)
        isCreate = false
        colorDialog.currentColor = colorField.text
        msg = ""
        open()
        nameField.forceActiveFocus()
        notifyChanged()
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 6

        QuiText {
            text: "类别名称"
            color: QuiColor.FontDark
        }

        QuiTextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: "输入类别名称"
            onTextChanged: control.notifyChanged()
        }

        QuiText {
            text: "颜色"
            color: QuiColor.FontDark
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            QuiTextField {
                id: colorField
                Layout.fillWidth: true
                placeholderText: "#RRGGBB"
                onTextChanged: control.notifyChanged()
            }

            Rectangle {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 32
                color: colorField.text
                border.width: 1
                border.color: "#CCCCCC"

                MouseArea {
                    anchors.fill: parent
                    onClicked: colorDialog.open()
                }
            }

            QuiButton {
                text: "选择颜色"
                onClicked: colorDialog.open()
            }
        }

        QuiText {
            text: "快捷键"
            color: QuiColor.FontDark
        }

        QuiTextField {
            id: shortcutField
            Layout.fillWidth: true
            placeholderText: "输入单个快捷键（可选）"
            onTextChanged: control.notifyChanged()
        }

        ColumnLayout {
            id: extraFieldsColumn
            Layout.fillWidth: true
            visible: control.extraFieldsHeight > 0
            spacing: 6
        }

        QuiText {
            text: "序号索引"
            color: QuiColor.FontDark
            visible: !isCreate
        }

        QuiTextField {
            id: ordinalField
            Layout.fillWidth: true
            visible: !isCreate
            placeholderText: "输入序号"
            validator: IntValidator {
                bottom: 0
                top: maxOrdinalIndex
            }
            onTextChanged: control.notifyChanged()
        }
    }

    onAccepted: {
        notifyChanged()
        if (isError()) {
            return
        }

        labelClassChangedAccepted(classId, className, classColor, classShortcut,
                                  isCreate ? maxOrdinalIndex : currentOrdinalIndex(), classGroup)
        close()
    }

    ColorDialog {
        id: colorDialog
        title: "选择颜色"
        onAccepted: {
            colorField.text = colorDialog.currentColor
            control.notifyChanged()
        }
    }
}
