import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

QuiPopup {
    id: control
    width: 420
    height: 330
    maskOpacity: 0.2

    property var frameworkModel: []
    property var architectureModel: []

    signal frameworkChanged(string frameworkName)
    signal submitted(string modelName, string frameworkName, string modelArchitecture)

    function openForm() {
        nameField.text = ""
        if (frameworkBox.count > 0) {
            frameworkBox.currentIndex = 0
            control.frameworkChanged(frameworkBox.currentText)
        }
        if (architectureBox.count > 0) {
            architectureBox.currentIndex = 0
        }
        messageText.text = ""
        open()
        nameField.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        QuiText {
            Layout.fillWidth: true
            text: "添加模型"
            font: QuiFont.Title
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            QuiText {
                text: "模型名称"
                color: QuiColor.FontDark
            }
            QuiTextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: "输入模型名称"
                onTextEdited: {
                    messageText.text = ""
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            QuiText {
                text: "框架"
                color: QuiColor.FontDark
            }
            QuiComboBox {
                id: frameworkBox
                Layout.fillWidth: true
                model: control.frameworkModel
                onActivated: {
                    control.frameworkChanged(currentText)
                    if (architectureBox.count > 0) {
                        architectureBox.currentIndex = 0
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            QuiText {
                text: "模型架构"
                color: QuiColor.FontDark
            }
            QuiComboBox {
                id: architectureBox
                Layout.fillWidth: true
                model: control.architectureModel
            }
        }

        QuiText {
            id: messageText
            Layout.fillWidth: true
            color: "#D83B01"
            visible: text.length > 0
        }

        Item {
            Layout.fillHeight: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Item {
                Layout.fillWidth: true
            }
            QuiButton {
                text: "取消"
                onClicked: control.close()
            }
            QuiButton {
                text: "确认"
                enabled: nameField.text.trim().length > 0 && frameworkBox.currentText.length > 0
                         && architectureBox.currentText.length > 0
                onClicked: {
                    const modelName = nameField.text.trim()
                    if (modelName.length === 0) {
                        messageText.text = "请输入模型名称"
                        return
                    }
                    control.submitted(modelName, frameworkBox.currentText, architectureBox.currentText)
                    control.close()
                }
            }
        }
    }
}
