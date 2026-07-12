import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

QuiPopup {
    id: control
    width: 600
    height: 480
    maskOpacity: 0.2

    property var frameworkModel: []
    property var architectureModel: []

    property string title: "添加模型"
    property bool metadataEditable: true
    property var nameValidator: null
    property string nameError: ""
    signal frameworkChanged(string frameworkName)
    signal submitted(string modelName, string frameworkName, string modelArchitecture)

    function validateName() {
        const modelName = nameField.text.trim()
        if (modelName.length === 0) {
            nameError = "请输入模型名称"
            return false
        }
        nameError = nameValidator ? nameValidator(modelName) : ""
        return nameError.length === 0
    }

    function openForm(modelName, frameworkName, modelArchitecture) {
        nameField.text = modelName === undefined ? "" : modelName
        if (frameworkBox.count > 0) {
            frameworkBox.currentIndex = frameworkName === undefined || frameworkName.length === 0
                                      ? 0 : Math.max(0, frameworkBox.find(frameworkName))
            if (metadataEditable) {
                control.frameworkChanged(frameworkBox.currentText)
            }
        }
        if (architectureBox.count > 0) {
            architectureBox.currentIndex = modelArchitecture === undefined || modelArchitecture.length === 0
                                        ? 0 : Math.max(0, architectureBox.find(modelArchitecture))
        }
        validateName()
        open()
        nameField.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        QuiText {
            Layout.fillWidth: true
            text: control.title
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
                onTextChanged: control.validateName()
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
                enabled: control.metadataEditable
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
                enabled: control.metadataEditable
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumHeight: 32
            Layout.preferredHeight: 32

            QuiText {
                id: messageText
                anchors.fill: parent
                text: control.nameError
                color: "#D83B01"
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignTop
                visible: text.length > 0
            }
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
                enabled: control.nameError.length === 0 && nameField.text.trim().length > 0
                         && frameworkBox.currentText.length > 0
                         && architectureBox.currentText.length > 0
                onClicked: {
                    const modelName = nameField.text.trim()
                    if (!control.validateName())
                        return
                    control.close()
                    control.submitted(modelName, frameworkBox.currentText, architectureBox.currentText)
                }
            }
        }
    }
}
