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
    property bool showSectionHeaders: true
    property real labelWidth: Math.min(280, Math.max(160, width * 0.3))

    property var fieldData: typeof model !== "undefined" ? model : ({})
    property string nameEn: String(roleValue("nameEn", ""))
    property string labelText: {
        const name = roleValue("nameCn", "")
        return name === undefined || name === null || String(name).length === 0 ? nameEn : String(name)
    }
    property string descriptionText: String(roleValue("description", "") || "").trim()
    property string sectionText: String(roleValue("section", "") || "")
    property string previousSectionText: {
        if (!fieldModel || rowIndex <= 0 || typeof fieldModel.fieldMap !== "function") {
            return ""
        }
        const previous = fieldModel.fieldMap(rowIndex - 1) || ({})
        return previous.section ? String(previous.section) : ""
    }
    property bool showCurrentSection: showSectionHeaders && sectionText.length > 0
            && sectionText !== previousSectionText
    property string valueType: String(roleValue("valueType", "string") || "string").toLowerCase()
    property string displayType: String(roleValue("displayType", "text") || "text").toLowerCase()
    property var valueRange: roleValue("valueRange", []) || []
    property var defaultValue: roleValue("defaultValue", "")
    property var configuredOptions: roleValue("options", []) || []
    property var configuredOptionsValueMap: roleValue("optionsValueMap", ({})) || ({})
    property var configuredOptionsMap: roleValue("optionsMap", ({})) || ({})
    property string optionsKeyField: String(roleValue("optionsKeyField", "") || "")
    property var currentValue: roleValue("value", defaultValue)
    property bool fieldEnabled: Utils.boolValue(roleValue("enabled", true), true)
    property bool fieldVisible: Utils.boolValue(roleValue("visible", true), true)
    property string unitText: String(roleValue("unit", "") || "")

    visible: fieldVisible
    implicitHeight: visible ? content.implicitHeight : 0

    onCurrentValueChanged: refreshEditor()

    function roleValue(name, fallbackValue) {
        const data = root.fieldData
        if (data && data[name] !== undefined && data[name] !== null) {
            return data[name]
        }
        return fallbackValue
    }

    function toBool(value) {
        return Utils.boolValue(value, false)
    }

    function stringValue(value) {
        return Utils.stringValue(value)
    }

    function rangeValue(index, fallback) {
        return Utils.numberValue(Utils.valueRangeAt(valueRange, index, fallback), fallback)
    }

    function isIntegerType() {
        return Utils.isIntegerValueType(valueType)
    }

    function numberValue(value, fallbackValue) {
        const fallback = fallbackValue === undefined ? 0 : Number(fallbackValue)
        let number = Number(value)
        if (!isFinite(number)) {
            number = isFinite(fallback) ? fallback : 0
        }
        return isIntegerType() ? Math.round(number) : number
    }

    function decimalsForStep() {
        return Utils.paramDecimals(valueType, valueRange, currentValue, defaultValue)
    }

    function hasOptionsMap() {
        return configuredOptionsMap && Object.keys(configuredOptionsMap).length > 0
    }

    function optionValueForLabel(label, fallbackValue) {
        const key = String(label)
        return configuredOptionsValueMap && configuredOptionsValueMap[key] !== undefined
                ? configuredOptionsValueMap[key] : fallbackValue
    }

    function optionEntry(raw) {
        if (raw && typeof raw === "object") {
            const label = raw.label !== undefined ? raw.label
                         : raw.key !== undefined ? raw.key
                         : raw.text !== undefined ? raw.text
                         : raw.name !== undefined ? raw.name : raw.value
            const value = raw.value !== undefined ? raw.value : optionValueForLabel(label, label)
            return { "label": String(label), "value": value }
        }

        const label = String(raw)
        return { "label": label, "value": optionValueForLabel(label, raw) }
    }

    function linkedOptions() {
        if (!fieldModel || optionsKeyField.length === 0) {
            return []
        }

        const linkedValue = stringValue(fieldModel.valueForName(optionsKeyField))
        if (typeof fieldModel.optionsForKey === "function") {
            const values = fieldModel.optionsForKey(nameEn, linkedValue)
            if (values && values.length > 0) {
                return values
            }
        }
        return configuredOptionsMap && configuredOptionsMap[linkedValue] !== undefined
                ? configuredOptionsMap[linkedValue] : []
    }

    function optionEntries() {
        let values = hasOptionsMap() ? linkedOptions() : configuredOptions
        if (!values || values.length === 0) {
            values = configuredOptions
        }

        const result = []
        const labels = []
        for (let i = 0; i < values.length; ++i) {
            const entry = optionEntry(values[i])
            if (labels.indexOf(entry.label) < 0) {
                labels.push(entry.label)
                result.push(entry)
            }
        }
        return result
    }

    function optionValuesEqual(lhs, rhs) {
        const leftNumber = Number(lhs)
        const rightNumber = Number(rhs)
        if (isFinite(leftNumber) && isFinite(rightNumber)
                && String(lhs).trim() !== "" && String(rhs).trim() !== "") {
            return leftNumber === rightNumber
        }
        return String(lhs) === String(rhs)
    }

    function optionIndexForValue(value, entries) {
        for (let i = 0; i < entries.length; ++i) {
            if (optionValuesEqual(entries[i].value, value)) {
                return i
            }
        }
        return -1
    }

    function usesSwitch() {
        return displayType === "switch" || displayType === "toggle"
    }

    function usesBool() {
        return usesSwitch() || displayType === "checkbox"
                || valueType === "bool" || valueType === "boolean"
    }

    function usesGroupCombo() {
        return displayType === "group_combo"
    }

    function usesCombo() {
        return displayType === "combo" || displayType === "combobox" || displayType === "select"
                || hasOptionsMap() || (configuredOptions && configuredOptions.length > 0)
    }

    function usesNumber() {
        const type = valueType
        return type === "int" || type === "integer" || type === "long"
                || type === "double" || type === "float" || type === "real"
    }

    function usesSlider() {
        return displayType === "slider"
    }

    function usesPath() {
        return displayType === "path" || displayType === "file" || displayType === "folder"
                || displayType === "directory" || displayType === "dir"
    }

    function usesFolderDialog() {
        return displayType === "folder" || displayType === "directory" || displayType === "dir"
    }

    function commit(value) {
        if (!fieldModel || !fieldEnabled || nameEn.length === 0) {
            return
        }
        fieldModel.setValueForName(nameEn, value)
    }

    function optionGroupsForField() {
        if (!fieldModel || typeof fieldModel.optionGroups !== "function") {
            return []
        }
        return fieldModel.optionGroups(rowIndex) || []
    }

    function refreshEditor() {
        if (editorLoader.item && editorLoader.item.refreshFromModel) {
            editorLoader.item.refreshFromModel()
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
            visible: root.showCurrentSection
            text: root.sectionText
            font: QuiFont.Subtitle
            color: QuiColor.FontDark
            elide: Text.ElideRight
        }

        Item {
            id: fieldRow

            Layout.fillWidth: true
            implicitHeight: Math.max(42, editorLoader.implicitHeight + 12)

            RowLayout {
                anchors.fill: parent
                spacing: 16

                ColumnLayout {
                    Layout.preferredWidth: root.labelWidth
                    Layout.maximumWidth: root.labelWidth
                    Layout.fillHeight: true
                    spacing: 2

                    Item { Layout.fillHeight: true }

                    RowLayout {
                        Layout.fillWidth: true

                        QuiTextIcon {
                            iconSource: QuiFontIcon.Info
                            visible: root.descriptionText.length > 0
                            Layout.preferredWidth: visible ? implicitWidth : 0

                            HoverHandler {
                                id: fieldInfoHover
                                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            }

                            QuiToolTip {
                                text: root.descriptionText
                                visible: fieldInfoHover.hovered && root.descriptionText.length > 0
                                delay: 200
                            }
                        }

                        QuiText {
                            Layout.fillWidth: true
                            text: root.labelText
                            color: root.fieldEnabled ? QuiColor.FontPrimary : QuiColor.FontDark
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                Loader {
                    id: editorLoader

                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    Layout.alignment: Qt.AlignVCenter
                    sourceComponent: {
                        if (root.usesGroupCombo()) {
                            return groupComboEditor
                        } else if (root.usesSwitch()) {
                            return switchEditor
                        } else if (root.usesBool()) {
                            return boolEditor
                        } else if (root.usesCombo()) {
                            return comboEditor
                        } else if (root.usesNumber()) {
                            return root.usesSlider() ? sliderEditor : numberEditor
                        } else if (root.usesPath()) {
                            return pathEditor
                        }
                        return textEditor
                    }
                }

                QuiText {
                    Layout.preferredWidth: visible ? Math.max(36, implicitWidth + 4) : 0
                    visible: root.unitText.length > 0
                    text: root.unitText
                    color: QuiColor.FontDark
                    elide: Text.ElideRight
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
        ignoreUnknownSignals: true

        function onValueChanged(changedName) {
            if (root.displayType === "group_combo"
                    || changedName === root.nameEn || changedName === root.optionsKeyField) {
                root.refreshEditor()
            }
        }
    }

    Component {
        id: boolEditor

        QuiCheckBox {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            enabled: root.fieldEnabled
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
            enabled: root.fieldEnabled
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
            enabled: root.fieldEnabled
            editable: false
            model: []
            textRole: "label"
            valueRole: "value"

            Component.onCompleted: refreshFromModel()
            onActivated: {
                if (currentIndex >= 0) {
                    root.commit(combo.currentValue)
                }
            }

            function refreshFromModel() {
                const values = root.optionEntries()
                model = values
                const index = root.optionIndexForValue(root.currentValue, values)
                if (index < 0 && root.hasOptionsMap() && values.length > 0) {
                    currentIndex = 0
                    root.commit(combo.currentValue)
                } else {
                    currentIndex = index
                }
            }
        }
    }

    Component {
        id: numberEditor

        QuiSpinEditor {
            anchors.fill: parent
            enabled: root.fieldEnabled
            value: root.numberValue(root.currentValue, root.numberValue(root.defaultValue, 0))
            minValue: root.rangeValue(0, root.isIntegerType() ? 0 : -1000000)
            maxValue: root.rangeValue(1, 1000000)
            step: root.rangeValue(2, root.isIntegerType() ? 1 : 0.1)
            decimals: root.isIntegerType() ? 0 : root.decimalsForStep()
            onEditingFinished: root.commit(root.numberValue(value, 0))

            function refreshFromModel() {
                value = root.numberValue(root.currentValue, root.numberValue(root.defaultValue, 0))
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
            property int decimals: root.isIntegerType() ? 0 : root.decimalsForStep()

            function refreshFromModel() {
                const next = root.numberValue(root.currentValue, root.numberValue(root.defaultValue, 0))
                slider.value = next
                editor.value = next
            }

            QuiSlider {
                id: slider

                Layout.fillWidth: true
                enabled: root.fieldEnabled
                from: sliderRow.minimum
                to: sliderRow.maximum
                stepSize: sliderRow.step
                precision: sliderRow.decimals
                value: root.numberValue(root.currentValue, root.numberValue(root.defaultValue, 0))
                onMoved: {
                    const next = root.numberValue(value, 0)
                    editor.value = next
                    root.commit(next)
                }
            }

            QuiSpinEditor {
                id: editor

                Layout.preferredWidth: 120
                Layout.fillHeight: true
                enabled: root.fieldEnabled
                value: root.numberValue(root.currentValue, root.numberValue(root.defaultValue, 0))
                minValue: sliderRow.minimum
                maxValue: sliderRow.maximum
                step: sliderRow.step
                decimals: sliderRow.decimals
                onEditingFinished: {
                    const next = root.numberValue(value, 0)
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
                const path = Utils.getCleanPath(url)
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
                    enabled: root.fieldEnabled
                    text: root.stringValue(root.currentValue)
                    placeholderText: root.usesFolderDialog() ? "选择目录" : "选择文件"
                    onEditingFinished: root.commit(text)
                }

                QuiTextIconButton {
                    Layout.preferredWidth: 34
                    Layout.fillHeight: true
                    enabled: root.fieldEnabled
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
            enabled: root.fieldEnabled
            text: root.stringValue(root.currentValue)
            onEditingFinished: root.commit(text)

            function refreshFromModel() {
                text = root.stringValue(root.currentValue)
            }
        }
    }

    Component {
        id: groupComboEditor

        QuiGroupComboBox {
            id: combo

            anchors.fill: parent
            enabled: root.fieldEnabled
            property var watchedValue: root.currentValue
            property string resolvedSavedValue: ""
            savedValue: resolvedSavedValue

            Component.onCompleted: refreshGroupOptions()

            function stripExtension(value) {
                const raw = String(value)
                const slash = Math.max(raw.lastIndexOf("/"), raw.lastIndexOf("\\"))
                const dot = raw.lastIndexOf(".")
                return dot > slash ? raw.substring(0, dot) : raw
            }

            function collapseDuplicateLabel(value) {
                const raw = value === undefined || value === null ? "" : String(value)
                const parts = raw.split(" / ")
                if (parts.length === 2 && parts[0] === parts[1]) {
                    return parts[0]
                }
                if (parts.length > 2 && parts.length % 2 === 0) {
                    const half = parts.length / 2
                    let duplicated = true
                    for (let i = 0; i < half; ++i) {
                        if (parts[i] !== parts[i + half]) {
                            duplicated = false
                            break
                        }
                    }
                    if (duplicated) {
                        return parts.slice(0, half).join(" / ")
                    }
                }
                return raw
            }

            function valueForSelection(value, groups) {
                const raw = collapseDuplicateLabel(value)
                for (let i = 0; i < groups.length; ++i) {
                    const group = groups[i]
                    const groupValue = group && group.value !== undefined ? String(group.value) : ""
                    if (groupValue === raw) {
                        return raw
                    }
                    const subs = group && group.subOptions ? group.subOptions : []
                    for (let j = 0; j < subs.length; ++j) {
                        const sub = subs[j]
                        const subValue = sub && sub.value !== undefined ? String(sub.value) : ""
                        const label = String(group.label) + " / " + String(sub.label)
                        if (subValue === raw || label === raw
                                || stripExtension(label) === stripExtension(raw)) {
                            return subValue
                        }
                    }
                }
                for (let i = 0; i < groups.length; ++i) {
                    const group = groups[i]
                    const groupValue = group && group.value !== undefined ? String(group.value) : ""
                    if (groupValue.length > 0 && stripExtension(raw) === stripExtension(groupValue)) {
                        return groupValue
                    }
                }
                return raw
            }

            function refreshGroupOptions() {
                const groups = root.optionGroupsForField()
                optionGroups = groups
                resolvedSavedValue = valueForSelection(root.currentValue, groups)
            }

            function refreshFromModel() {
                refreshGroupOptions()
            }

            onWatchedValueChanged: {
                if (!popup.opened) {
                    refreshGroupOptions()
                }
            }

            onAboutToOpen: refreshGroupOptions()

            onCommit: function(value) {
                resolvedSavedValue = value
                root.commit(value)
            }
        }
    }
}
