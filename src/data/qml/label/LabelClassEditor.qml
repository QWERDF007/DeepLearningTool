import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// import QtQuick.Dialogs
import Qt.labs.platform

import dltool.ui

DltPopup {
    id: control
    closePolicy: Popup.CloseOnPressOutside
    width: 480
    height: 360
    maskOpacity: 0.2

    property int classId: -1
    property alias className: nameField.text
    property alias classColor: colorField.text
    property alias classShortcut: shortcutField.text
    property alias ordinalIndex: ordinalField.text
    property string msg: ""
    property int maxOrdinalIndex: 0

    property bool isCreate: true
    
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

    signal labelClassChanged(int classId, string className, string classColor, string classShortcut, int ordinalIndex)
    signal labelClassChangedAccepted(int classId, string className, string classColor, string classShortcut, int ordinalIndex)

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

                DltText {
                    text: "名字"
                }
                DltTextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: "输入类别名称"
                    onTextEdited: {
                        labelClassChanged(classId, className, classColor, classShortcut, isCreate ? -1 : ordinalIndex)
                    }
                }
                DltText {
                    text: "颜色"
                }
                DltTextField {
                    id: colorField
                    Layout.fillWidth: true
                    placeholderText: "#RRGGBB"
                    text: colorDialog.currentColor
                    onTextEdited: {
                        labelClassChanged(classId, className, classColor, classShortcut, isCreate ? -1 : ordinalIndex)
                    }
                }
                DltText {
                    text: "快捷键"
                }
                DltTextField {
                    id: shortcutField
                    Layout.fillWidth: true
                    placeholderText: "输入快捷键"
                    onTextEdited: {
                        labelClassChanged(classId, className, classColor, classShortcut, isCreate ? -1 : ordinalIndex)
                    }
                }
                DltText {
                    text: "序号索引"
                    visible: !isCreate
                }
                DltTextField {
                    id: ordinalField
                    visible: !isCreate
                    Layout.fillWidth: true
                    placeholderText: "输入序号"
                    validator: IntValidator {
                        bottom: 0
                        top: maxOrdinalIndex
                    }
                    onTextEdited: {
                        labelClassChanged(classId, className, classColor, classShortcut, isCreate ? -1 : ordinalIndex)
                    }
                }
                Item {
                    Layout.fillHeight: true
                }
            }

            ColumnLayout {
                Layout.fillHeight: true
                Layout.preferredWidth: parent.width * 0.4
                DltText {
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
            DltButton {
                text: "取消"
                onClicked: {
                    clearInput()
                    control.close()
                }
            }
            DltButton {
                text: "确认"
                enabled: !control.isError()
                onClicked: {
                    labelClassChangedAccepted(classId, nameField.text, colorField.text, shortcutField.text, isCreate ? maxOrdinalIndex : ordinalField.text)
                    clearInput()
                    control.close()
                }
            }
        }

        DltText {
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
        }
    }

    function clearInput() {
        nameField.text = ""
        colorField.text = "#000000"
        shortcutField.text = ""
        ordinalField.text = ""
        control.msg = ""
    }
}
