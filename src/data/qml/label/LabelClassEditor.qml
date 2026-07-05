import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
// import QtQuick.Dialogs
import Qt.labs.platform

import dltool.ui

QuiPopup {
    id: control
    closePolicy: Popup.CloseOnPressOutside
    width: 480
    height: groupEditable ? 410 : 360
    maskOpacity: 0.2

    property int classId: -1
    property alias className: nameField.text
    property alias classColor: colorField.text
    property alias classShortcut: shortcutField.text
    property alias ordinalIndex: ordinalField.text
    property string classGroup: "anomaly"
    property bool groupEditable: false
    property string msg: ""
    property int maxOrdinalIndex: 0

    property bool isCreate: true

    onClassGroupChanged: {
        let idx = groupIndex(classGroup)
        if (groupBox.currentIndex !== idx) {
            groupBox.currentIndex = idx
        }
    }
    
    // 判断消息是否为错误（阻止提交）
    function isError() {
        return msg.startsWith("error:")
    }
    
    // 获取显示的消息文本（去掉前缀）
    function getDisplayMsg() {
        if (msg.startsWith("error:")) {
            return msg.substring(6)
        } else if (msg.startsWith("warning:")) {
            return msg.substring(8)
        }
        return msg
    }
    
    // 获取消息颜色
    function getMsgColor() {
        if (msg.startsWith("error:")) {
            return "red"
        } else if (msg.startsWith("warning:")) {
            return "orange"
        }
        return "red"
    }

    function normalizedGroup(group) {
        return group === "good" || group === "良好" ? "good" : "anomaly"
    }

    function groupIndex(group) {
        return normalizedGroup(group) === "good" ? 1 : 0
    }

    function notifyChanged() {
        labelClassChanged(classId, className, classColor, classShortcut, isCreate ? -1 : ordinalIndex, classGroup)
    }

    signal labelClassChanged(int classId, string className, string classColor, string classShortcut, int ordinalIndex, string classGroup)
    signal labelClassChangedAccepted(int classId, string className, string classColor, string classShortcut, int ordinalIndex, string classGroup)

    Item {
        anchors.fill: parent
        anchors.margins: 10
        // spacing: 10
        RowLayout {
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                bottom: buttonRow.top
                bottomMargin: 10
            }
            // 左侧输入区域
            ColumnLayout {
                Layout.fillHeight: true
                Layout.preferredWidth: parent.width * 0.6
                spacing: 5

                QuiText {
                    text: "名字"
                }
                QuiTextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: "输入类别名称"
                    onTextEdited: {
                        notifyChanged()
                    }
                }
                QuiText {
                    text: "颜色"
                }
                QuiTextField {
                    id: colorField
                    Layout.fillWidth: true
                    placeholderText: "#RRGGBB"
                    text: colorDialog.currentColor
                    onTextEdited: {
                        notifyChanged()
                    }
                }
                QuiText {
                    text: "快捷键"
                }
                QuiTextField {
                    id: shortcutField
                    Layout.fillWidth: true
                    placeholderText: "输入快捷键"
                    onTextEdited: {
                        notifyChanged()
                    }
                }
                QuiText {
                    text: "分组"
                    visible: groupEditable
                }
                QuiComboBox {
                    id: groupBox
                    visible: groupEditable
                    Layout.fillWidth: true
                    model: ["异常", "良好"]
                    currentIndex: control.groupIndex(control.classGroup)
                    onActivated: function(index) {
                        control.classGroup = index === 1 ? "good" : "anomaly"
                        control.notifyChanged()
                    }
                }
                QuiText {
                    text: "序号索引"
                    visible: !isCreate
                }
                QuiTextField {
                    id: ordinalField
                    visible: !isCreate
                    Layout.fillWidth: true
                    placeholderText: "输入序号"
                    validator: IntValidator {
                        bottom: 0
                        top: maxOrdinalIndex
                    }
                    onTextEdited: {
                        notifyChanged()
                    }
                }
                Item {
                    Layout.fillHeight: true
                }
            }

            ColumnLayout {
                Layout.fillHeight: true
                Layout.preferredWidth: parent.width * 0.4
                QuiText {
                    text: "选择颜色"
                }
                Rectangle {
                    Layout.fillHeight: true
                    Layout.fillWidth: true

                    color: colorField.text
                    border.width: 1
                    border.color: "#CCCCCC"

                    MouseArea {
                        anchors.fill: parent
                        onClicked: colorDialog.open()
                    }
                }
            }
        }

        // 底部按钮
        RowLayout {
            id: buttonRow
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            spacing: 10
            Item {
                Layout.fillWidth: true
            }
            QuiButton {
                text: "取消"
                onClicked: {
                    clearInput()
                    control.close()
                }
            }
            QuiButton {
                text: "确认"
                enabled: !control.isError()
                onClicked: {
                    labelClassChanged(classId, nameField.text, colorField.text, shortcutField.text, isCreate ? -1 : ordinalField.text, classGroup)
                    if (control.isError()) {
                        return
                    }
                    labelClassChangedAccepted(classId, nameField.text, colorField.text, shortcutField.text, isCreate ? maxOrdinalIndex : ordinalField.text, classGroup)
                    clearInput()
                    control.close()
                }
            }
        }

        QuiText {
            id: msgText
            text: control.getDisplayMsg()
            anchors {
                bottom: parent.bottom
                left: parent.left
                right: buttonRow.left
                rightMargin: 5
            }
            color: control.getMsgColor()
            visible: control.msg ? true : false
        }
    }

    ColorDialog {
        id: colorDialog
        title: "选择颜色"
        onAccepted: {
            colorField.text = colorDialog.currentColor
            notifyChanged()
        }
    }

    function openForCreate(defaultColor, defaultGroup) {
        clearInput(defaultColor, defaultGroup)
        control.classId = -1
        control.isCreate = true
        colorDialog.currentColor = defaultColor
        colorField.text = defaultColor
        control.open()
    }

    function clearInput(defaultColor, defaultGroup) {
        nameField.text = ""
        colorField.text = defaultColor !== undefined ? defaultColor : "#000000"
        shortcutField.text = ""
        ordinalField.text = ""
        classGroup = defaultGroup !== undefined ? normalizedGroup(defaultGroup) : "anomaly"
        control.msg = ""
    }
}
