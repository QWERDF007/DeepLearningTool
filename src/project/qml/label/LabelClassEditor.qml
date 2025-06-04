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
    height: 280
    maskOpacity: 0.2

    property int classId: -1
    property alias className: nameField.text
    property alias classColor: colorField.text
    property alias classShortcut: shortcutField.text
    property int ordinalIndex: -1

    signal editFinished(int classId, string className, string classColor, string classShortcut, int ordinalIndex)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10
        RowLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
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
                }
                DltText {
                    text: "颜色"
                }
                DltTextField {
                    id: colorField
                    Layout.fillWidth: true
                    placeholderText: "#RRGGBB"
                    text: colorDialog.currentColor
                }
                DltText {
                    text: "快捷键"
                }
                DltTextField {
                    id: shortcutField
                    Layout.fillWidth: true
                    placeholderText: "输入快捷键"
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
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 10
            Item {
                Layout.fillWidth: true
            }
            DltButton {
                text: "取消"
                onClicked: control.close()
            }
            DltButton {
                text: "确认"
                onClicked: {
                    editFinished(classId, nameField.text, colorField.text, shortcutField.text, ordinalIndex)
                    control.close()
                }
            }
        }
    }

    ColorDialog {
        id: colorDialog
        title: "选择颜色"
        onAccepted: {
            colorField.text = colorDialog.currentColor
        }
    }
}
