import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.data
import dltool.ui
import quickui

QuiPopup {
    id: control

    width: 600
    height: 420
    maskOpacity: 0.2

    property DataManager dataManager: null
    property int datasetId: -1
    property string datasetName: ""
    property string validationError: ""
    property bool waiting: false

    readonly property bool canSubmit: !waiting
                                      && control.validationError.length === 0
                                      && trainRatioField.text.trim().length > 0
                                      && testRatioField.text.trim().length > 0
                                      && (!useValidationCheckBox.checked
                                          || validationRatioField.text.trim().length > 0)

    function ratioValue(field, label) {
        const value = field.text.trim()
        if (value.length === 0) {
            validationError = "请输入" + label + "比例"
            return NaN
        }

        const ratio = Number(value)
        if (!isFinite(ratio) || ratio < 0 || ratio > 1) {
            validationError = label + "比例必须是 0 到 1 之间的数字"
            return NaN
        }
        return ratio
    }

    function validateRatios() {
        validationError = ""
        const train = ratioValue(trainRatioField, "训练集")
        if (isNaN(train)) {
            return false
        }
        const test = ratioValue(testRatioField, "测试集")
        if (isNaN(test)) {
            return false
        }

        let validation = 0
        if (useValidationCheckBox.checked) {
            validation = ratioValue(validationRatioField, "验证集")
            if (isNaN(validation)) {
                return false
            }
        }

        const sum = train + validation + test
        if (Math.abs(sum - 1.0) > 0.000000001) {
            validationError = "训练集、验证集和测试集比例之和必须等于 1"
            return false
        }
        if (train <= 0 || test <= 0 || (useValidationCheckBox.checked && validation <= 0)) {
            validationError = "启用的子集比例必须大于 0"
            return false
        }
        return true
    }

    function openForDataset(id, name) {
        datasetId = id
        datasetName = name === undefined ? "" : name
        trainRatioField.text = "0.8"
        validationRatioField.text = "0.0"
        testRatioField.text = "0.2"
        useValidationCheckBox.checked = false
        validationError = ""
        waiting = false
        open()
        trainRatioField.forceActiveFocus()
    }

    Connections {
        target: control.dataManager
        function onDatasetSplitFinished(success, message) {
            if (!control.waiting) {
                return
            }
            control.waiting = false
            if (!success) {
                control.validationError = message
                control.open()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        QuiText {
            Layout.fillWidth: true
            text: "划分数据集"
            font: QuiFont.Subtitle
        }

        QuiText {
            Layout.fillWidth: true
            text: control.datasetName
            color: QuiColor.FontDark
            elide: Text.ElideRight
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            QuiText {
                text: "训练集比例"
                color: QuiColor.FontDark
            }
            QuiTextField {
                id: trainRatioField
                Layout.fillWidth: true
                placeholderText: "例如 0.8"
                onTextChanged: control.validateRatios()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            QuiCheckBox {
                id: useValidationCheckBox
                Layout.fillWidth: true
                text: "使用验证集"
                onCheckedChanged: control.validateRatios()
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: useValidationCheckBox.checked

                QuiText {
                    text: "验证集比例"
                    color: QuiColor.FontDark
                }
                QuiTextField {
                    id: validationRatioField
                    Layout.fillWidth: true
                    placeholderText: "例如 0.1"
                    onTextChanged: control.validateRatios()
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            QuiText {
                text: "测试集比例"
                color: QuiColor.FontDark
            }
            QuiTextField {
                id: testRatioField
                Layout.fillWidth: true
                placeholderText: "例如 0.2"
                onTextChanged: control.validateRatios()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumHeight: 32
            Layout.preferredHeight: 32

            QuiText {
                anchors.fill: parent
                text: control.validationError
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
                enabled: !control.waiting
                onClicked: control.close()
            }

            QuiButton {
                text: "确认"
                enabled: control.canSubmit
                onClicked: {
                    if (!control.validateRatios() || !control.dataManager || control.datasetId < 0) {
                        return
                    }
                    control.waiting = true
                    control.close()
                    control.dataManager.splitDataset(control.datasetId,
                                                     Number(trainRatioField.text),
                                                     useValidationCheckBox.checked
                                                         ? Number(validationRatioField.text) : 0,
                                                     Number(testRatioField.text),
                                                     useValidationCheckBox.checked)
                }
            }
        }
    }
}
