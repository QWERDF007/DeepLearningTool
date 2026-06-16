import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import quickui

Item {
    id: root

    property var fieldModel: null
    property int rowIndex: typeof index === "number" ? index : -1
    property string nameEn: model.nameEn || ""
    property string labelText: model.nameCn && model.nameCn.length > 0 ? model.nameCn : model.nameEn
    property string descriptionText: model.description ? String(model.description) : ""
    property string sectionText: model.section ? String(model.section) : ""
    property string previousSectionText: {
        if (!fieldModel || rowIndex <= 0) {
            return ""
        }
        let previous = fieldModel.fieldMap(rowIndex - 1)
        return previous && previous.section ? String(previous.section) : ""
    }
    property bool showSectionHeader: sectionText.length > 0 && sectionText !== previousSectionText
    property string valueType: String(model.valueType || "string").toLowerCase()
    property string controlType: String(model.controlType || "").toLowerCase()
    property var valueRange: model.valueRange || []
    property var configuredOptions: model.options || []
    property var currentValue: model.value

    visible: model.visible === undefined ? true : model.visible
    implicitHeight: visible ? content.implicitHeight : 0

    function toBool(value) {
        if (typeof value === "string") {
            let text = value.trim().toLowerCase()
            return text === "true" || text === "1" || text === "yes" || text === "on"
        }
        return Boolean(value)
    }

    function stringValue(value) {
        if (value === undefined || value === null) {
            return ""
        }
        return String(value)
    }

    function rangeValue(index, fallback) {
        return valueRange && valueRange.length > index ? Number(valueRange[index]) : fallback
    }

    function isIntegerType() {
        return valueType === "int" || valueType === "integer"
    }

    function numberValue(value) {
        let number = Number(value)
        if (!isFinite(number)) {
            number = 0
        }
        return isIntegerType() ? Math.round(number) : number
    }

    function decimalsForStep() {
        if (isIntegerType()) {
            return 0
        }
        let stepText = String(rangeValue(2, 0.1))
        let dot = stepText.indexOf(".")
        return dot >= 0 ? Math.min(6, stepText.length - dot - 1) : 4
    }

    function formattedNumber(value) {
        let number = numberValue(value)
        return isIntegerType() ? String(Math.round(number)) : Number(number.toFixed(decimalsForStep())).toString()
    }

    function hasOptionsMap() {
        let map = model.optionsMap
        return map && Object.keys(map).length > 0
    }

    function optionValues() {
        let values = []
        if (fieldModel && hasOptionsMap()) {
            let modelName = String(fieldModel.valueForName("model") || "")
            let mapped = fieldModel.optionsForKey(nameEn, modelName)
            if (mapped && mapped.length > 0) {
                values = mapped
            }
        }
        if (values.length === 0 && configuredOptions) {
            values = configuredOptions
        }

        let result = []
        for (let i = 0; i < values.length; ++i) {
            let text = String(values[i])
            if (result.indexOf(text) < 0) {
                result.push(text)
            }
        }

        return result
    }

    function usesBool() {
        return controlType === "checkbox" || controlType === "switch" || controlType === "toggle"
                || valueType === "bool" || valueType === "boolean"
    }

    function usesSwitch() {
        return controlType === "switch" || controlType === "toggle"
    }

    function usesCombo() {
        return controlType === "combo" || controlType === "combobox" || controlType === "select" || hasOptionsMap()
                || (configuredOptions && configuredOptions.length > 0)
    }

    function usesNumber() {
        return valueType === "int" || valueType === "integer" || valueType === "double" || valueType === "float" || valueType === "real"
    }

    function usesSlider() {
        return controlType === "slider"
    }

    function usesPath() {
        return controlType === "path" || controlType === "file" || controlType === "folder" || controlType === "directory"
    }

    function usesFolderDialog() {
        return controlType === "folder" || controlType === "directory"
    }

    function commit(value) {
        if (fieldModel) {
            fieldModel.setValueForName(nameEn, value)
        }
    }

    ColumnLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 0

        QuiText {
            Layout.fillWidth: true
            Layout.topMargin: root.rowIndex > 0 ? 14 : 0
            Layout.bottomMargin: 6
            visible: root.showSectionHeader
            text: root.sectionText
            font: QuiFont.Subtitle
            color: QuiColor.Highlight
            elide: Text.ElideRight
        }

        Item {
            id: fieldRow

            Layout.fillWidth: true
            implicitHeight: Math.max(root.descriptionText.length > 0 ? 58 : 42, editorLoader.implicitHeight + 12)

            RowLayout {
                anchors.fill: parent
                spacing: 16

                ColumnLayout {
                    Layout.preferredWidth: Math.min(280, Math.max(160, root.width * 0.3))
                    Layout.maximumWidth: Math.min(280, Math.max(160, root.width * 0.3))
                    Layout.fillHeight: true
                    spacing: 2

                    Item {
                        Layout.fillHeight: true
                    }

                    QuiText {
                        Layout.fillWidth: true
                        text: root.labelText
                        color: QuiColor.FontPrimary
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    QuiText {
                        Layout.fillWidth: true
                        visible: root.descriptionText.length > 0
                        text: root.descriptionText
                        color: QuiColor.FontDark
                        font: QuiFont.Caption
                        elide: Text.ElideRight
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }

                Loader {
                    id: editorLoader

                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    Layout.alignment: Qt.AlignVCenter
                    sourceComponent: root.usesSwitch()
                                     ? switchEditor
                                     : root.usesBool()
                                       ? boolEditor
                                       : root.usesCombo()
                                         ? comboEditor
                                         : root.usesNumber()
                                           ? (root.usesSlider() ? sliderEditor : numberEditor)
                                           : root.usesPath()
                                             ? pathEditor
                                             : textEditor
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: QuiColor.Border
                opacity: 0.55
            }
        }
    }

    Connections {
        target: root.fieldModel
        function onValueChanged(changedName, value) {
            if (changedName === "model" || changedName === root.nameEn) {
                if (editorLoader.item && editorLoader.item.refreshFromModel) {
                    editorLoader.item.refreshFromModel()
                }
            }
        }
    }

    Component {
        id: boolEditor

        QuiCheckBox {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: ""
            checked: root.toBool(root.currentValue)
            onToggled: root.commit(checked)

            function refreshFromModel() {
                checked = root.toBool(root.currentValue)
            }
        }
    }

    Component {
        id: switchEditor

        QuiToggleSwitch {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: ""
            checked: root.toBool(root.currentValue)
            clickListener: function () {
                checked = !checked
                root.commit(checked)
            }

            function refreshFromModel() {
                checked = root.toBool(root.currentValue)
            }
        }
    }

    Component {
        id: comboEditor

        QuiComboBox {
            id: combo

            anchors.fill: parent
            editable: false
            model: []

            Component.onCompleted: refreshFromModel()
            onActivated: root.commit(currentText)

            function refreshFromModel() {
                model = root.optionValues()
                let text = root.stringValue(root.currentValue)
                currentIndex = find(text)
            }
        }
    }

    Component {
        id: numberEditor

        QuiSpinEditor {
            anchors.fill: parent
            value: root.numberValue(root.currentValue)
            minValue: root.rangeValue(0, root.isIntegerType() ? 0 : -1000000)
            maxValue: root.rangeValue(1, 1000000)
            step: root.rangeValue(2, root.isIntegerType() ? 1 : 0.1)
            decimals: root.decimalsForStep()
            onEditingFinished: root.commit(root.numberValue(value))

            function refreshFromModel() {
                value = root.numberValue(root.currentValue)
            }
        }
    }

    Component {
        id: sliderEditor

        RowLayout {
            id: sliderRow

            anchors.fill: parent
            spacing: 8

            property real minimum: root.rangeValue(0, root.isIntegerType() ? 0 : -1000000)
            property real maximum: root.rangeValue(1, 1000000)
            property real step: root.rangeValue(2, root.isIntegerType() ? 1 : 0.1)
            property int decimals: root.decimalsForStep()

            function refreshFromModel() {
                let next = root.numberValue(root.currentValue)
                slider.value = next
                editor.value = next
            }

            QuiSlider {
                id: slider

                Layout.fillWidth: true
                from: sliderRow.minimum
                to: sliderRow.maximum
                stepSize: sliderRow.step
                precision: sliderRow.decimals
                value: root.numberValue(root.currentValue)
                onMoved: {
                    let next = root.numberValue(value)
                    editor.value = next
                    root.commit(next)
                }
            }

            QuiSpinEditor {
                id: editor

                Layout.preferredWidth: 120
                Layout.fillHeight: true
                value: root.numberValue(root.currentValue)
                minValue: sliderRow.minimum
                maxValue: sliderRow.maximum
                step: sliderRow.step
                decimals: sliderRow.decimals
                onEditingFinished: {
                    let next = root.numberValue(value)
                    slider.value = next
                    root.commit(next)
                }
            }
        }
    }

    Component {
        id: pathEditor

        Item {
            id: pathRoot

            anchors.fill: parent

            function refreshFromModel() {
                pathInput.text = root.stringValue(root.currentValue)
            }

            function commitPath(url) {
                let path = Utils.getCleanPath(url)
                pathInput.text = path
                root.commit(path)
            }

            RowLayout {
                anchors.fill: parent
                spacing: 8

                QuiTextField {
                    id: pathInput

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: root.stringValue(root.currentValue)
                    placeholderText: root.usesFolderDialog() ? "选择目录" : "选择文件"
                    onEditingFinished: root.commit(text)
                }

                QuiTextIconButton {
                    Layout.preferredWidth: 34
                    Layout.fillHeight: true
                    iconSource: root.usesFolderDialog() ? QuiFontIcon.OpenFolderHorizontal : QuiFontIcon.OpenFile
                    text: root.usesFolderDialog() ? "选择目录" : "选择文件"
                    onClicked: root.usesFolderDialog() ? folderDialog.open() : fileDialog.open()
                }
            }

            FileDialog {
                id: fileDialog

                title: "选择文件"
                onAccepted: pathRoot.commitPath(fileDialog.file.toString())
            }

            FolderDialog {
                id: folderDialog

                title: "选择目录"
                onAccepted: pathRoot.commitPath(folderDialog.folder.toString())
            }
        }
    }

    Component {
        id: textEditor

        QuiTextField {
            anchors.fill: parent
            text: root.stringValue(root.currentValue)
            onEditingFinished: root.commit(text)

            function refreshFromModel() {
                text = root.stringValue(root.currentValue)
            }
        }
    }
}
