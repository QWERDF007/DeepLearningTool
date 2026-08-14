import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Templates as T
import dltool.ui
import dltool.model
import quickui

Item {
    id: control

    property IParams params: null
    property int targetPartIndex: 0
    property int partSpacing: 5

    QuiScrollablePage {
        anchors.fill: parent
        animationEnabled: false
        padding: 0

        Repeater {
            model: control.params

            Loader {
                id: groupLoader

                property int groupPartIndex: Utils.numberValue(groupRole("partIndex", 0), 0)
                property var groupModel: groupRole("groupModel", null)
                property string groupNameEn: groupRole("nameEn", "")
                property string groupLabel: groupRole("nameCn", groupNameEn)
                property string groupDescription: groupRole("description", "")
                property bool groupVisible: groupPartIndex === control.targetPartIndex

                function groupRole(roleName, fallbackValue) {
                    if (typeof model !== "undefined" && model && model[roleName] !== undefined && model[roleName] !== null) {
                        return model[roleName];
                    }
                    return fallbackValue;
                }

                active: groupVisible
                visible: active
                Layout.fillWidth: true
                Layout.preferredHeight: active && item ? item.implicitHeight : 0
                Layout.maximumHeight: active ? Number.POSITIVE_INFINITY : 0
                sourceComponent: groupComponent

                Component {
                    id: groupComponent

                    Rectangle {
                        id: groupRoot

                        property var groupModel: groupLoader.groupModel
                        property string groupLabel: groupLoader.groupLabel
                        property string groupDescription: groupLoader.groupDescription

                        implicitHeight: groupContent.implicitHeight + 2 * control.partSpacing
                        radius: 4
                        clip: true
                        color: QuiColor.Primary

                        ColumnLayout {
                            id: groupContent

                            anchors.fill: parent
                            anchors.margins: control.partSpacing
                            spacing: control.partSpacing

                            QuiText {
                                Layout.fillWidth: true
                                text: groupRoot.groupLabel
                                font: QuiFont.Subtitle
                                elide: Text.ElideRight
                            }

                            QuiText {
                                Layout.fillWidth: true
                                text: groupRoot.groupDescription
                                visible: text.length > 0
                                color: QuiColor.FontDark
                                font: QuiFont.Caption
                                wrapMode: Text.Wrap
                            }

                            ListView {
                                id: paramList

                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(contentHeight, 48)
                                spacing: control.partSpacing
                                interactive: false
                                clip: true
                                model: groupRoot.groupModel

                                delegate: Item {
                                    id: delegateRoot

                                    width: ListView.view ? ListView.view.width : 0
                                    height: 40

                                    property var groupModel: groupRoot.groupModel
                                    property int rowIndex: index
                                    property real labelWidth: Math.max(0, Math.floor(width / 3))
                                    property string paramNameEn: modelValue("nameEn", "")
                                    property string paramLabel: paramNameEn
                                    property string paramDescription: modelValue("description", "").trim()
                                    property bool hasParamDescription: paramDescription.length > 0
                                    property string paramDisplayType: modelValue("displayType", "text")
                                    property string paramValueType: modelValue("valueType", "string")
                                    property var paramValue: modelValue("value", modelValue("defaultValue", ""))
                                    property var paramDefaultValue: modelValue("defaultValue", "")
                                    property var paramValueRange: modelValue("valueRange", [])
                                    property var paramMinimumValue: Utils.valueRangeAt(paramValueRange, 0, 0)
                                    property var paramMaximumValue: Utils.valueRangeAt(paramValueRange, 1, 100)
                                    property var paramStepValue: Utils.valueRangeAt(paramValueRange, 2, 1)
                                    property int paramDecimals: Utils.paramDecimals(paramValueType, paramValueRange, paramValue, paramDefaultValue)
                                    property bool paramEnabled: Utils.boolValue(modelValue("enabled", true), true)
                                    property var paramOptions: modelValue("options", [])
                                    property var paramOptionsValueMap: modelValue("optionsValueMap", ({}))
                                    property string paramUnit: modelValue("unit", "")

                                    function modelValue(roleName, fallbackValue) {
                                        if (typeof model !== "undefined" && model && model[roleName] !== undefined && model[roleName] !== null) {
                                            return model[roleName];
                                        }
                                        return fallbackValue;
                                    }

                                    function commitValue(value) {
                                        if (groupModel && rowIndex >= 0) {
                                            groupModel.setValue(rowIndex, value);
                                        }
                                    }

                                    function optionLabel(option) {
                                        if (option && typeof option === "object") {
                                            if (option.label !== undefined)
                                                return String(option.label)
                                            if (option.text !== undefined)
                                                return String(option.text)
                                            if (option.name !== undefined)
                                                return String(option.name)
                                            if (option.value !== undefined)
                                                return String(option.value)
                                        }
                                        return String(option)
                                    }

                                    function optionValue(option) {
                                        if (option && typeof option === "object" && option.value !== undefined)
                                            return option.value

                                        const key = optionLabel(option)
                                        return paramOptionsValueMap && paramOptionsValueMap[key] !== undefined
                                                ? paramOptionsValueMap[key] : option
                                    }

                                    function optionIndexForValue(value) {
                                        const options = paramOptions || []
                                        for (let i = 0; i < options.length; ++i) {
                                            if (String(optionValue(options[i])) === String(value))
                                                return i
                                        }
                                        return -1
                                    }

                                    function editorComponent(type) {
                                        if (type === "spin") {
                                            return spinEditorComponent;
                                        }
                                        if (type === "slider") {
                                            return sliderEditorComponent;
                                        }
                                        if (type === "checkbox") {
                                            return checkEditorComponent;
                                        }
                                        if (type === "combo") {
                                            return comboEditorComponent;
                                        }
                                        if (type === "group_combo") {
                                            return weightComboEditorComponent;
                                        }
                                        return textEditorComponent;
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: control.partSpacing

                                        ColumnLayout {
                                            Layout.preferredWidth: delegateRoot.labelWidth
                                            Layout.maximumWidth: delegateRoot.labelWidth
                                            Layout.fillHeight: true
                                            spacing: 2

                                            RowLayout {
                                                Layout.fillWidth: true

                                                QuiTextIcon {
                                                    iconSource: QuiFontIcon.Info
                                                    visible: delegateRoot.hasParamDescription
                                                    Layout.preferredWidth: visible ? implicitWidth : 0

                                                    HoverHandler {
                                                        id: paramInfoHover

                                                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                                    }

                                                    QuiToolTip {
                                                        text: delegateRoot.paramDescription
                                                        visible: paramInfoHover.hovered && delegateRoot.hasParamDescription
                                                        delay: 200
                                                    }
                                                }

                                                QuiText {
                                                    Layout.fillWidth: true
                                                    text: delegateRoot.paramLabel
                                                    color: delegateRoot.paramEnabled
                                                           ? QuiColor.FontPrimary
                                                           : QuiColor.FontDark
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 36

                                            RowLayout {
                                                anchors.fill: parent
                                                spacing: control.partSpacing

                                                Loader {
                                                    Layout.fillWidth: true
                                                    Layout.fillHeight: true
                                            sourceComponent: delegateRoot.editorComponent(delegateRoot.paramDisplayType)
                                                }

                                                QuiText {
                                                    Layout.preferredWidth: visible ? 36 : 0
                                                    visible: delegateRoot.paramUnit.length > 0
                                                    text: delegateRoot.paramUnit
                                                    color: QuiColor.FontDark
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 1
                                        color: QuiColor.Border
                                        opacity: 0.6
                                    }

                                    Component {
                                        id: spinEditorComponent

                                        QuiSpinEditor {
                                            anchors.fill: parent
                                            enabled: delegateRoot.paramEnabled
                                            value: Utils.numberValue(delegateRoot.paramValue, Utils.numberValue(delegateRoot.paramDefaultValue, 0))
                                            minValue: Utils.numberValue(delegateRoot.paramMinimumValue, 0)
                                            maxValue: Utils.numberValue(delegateRoot.paramMaximumValue, 10000)
                                            step: Utils.numberValue(delegateRoot.paramStepValue, 1)
                                            decimals: Utils.isIntegerValueType(delegateRoot.paramValueType) ? 0 : delegateRoot.paramDecimals
                                            onEditingFinished: {
                                                const nextValue = Utils.isIntegerValueType(delegateRoot.paramValueType) ? Math.round(value) : value;
                                                delegateRoot.commitValue(nextValue);
                                            }
                                        }
                                    }

                                    Component {
                                        id: sliderEditorComponent

                                        RowLayout {
                                            anchors.fill: parent
                                            spacing: control.partSpacing

                                            QuiSlider {
                                                id: slider
                                                Layout.fillWidth: true
                                                enabled: delegateRoot.paramEnabled
                                                from: Utils.numberValue(delegateRoot.paramMinimumValue, 0)
                                                to: Utils.numberValue(delegateRoot.paramMaximumValue, 1)
                                                value: Utils.numberValue(delegateRoot.paramValue, Utils.numberValue(delegateRoot.paramDefaultValue, 0))
                                                stepSize: Utils.numberValue(delegateRoot.paramStepValue, 0.01)
                                                precision: delegateRoot.paramDecimals
                                                onMoved: delegateRoot.commitValue(value)
                                            }

                                            QuiText {
                                                Layout.preferredWidth: 54
                                                horizontalAlignment: Text.AlignRight
                                                color: delegateRoot.paramEnabled ? QuiColor.FontPrimary : QuiColor.FontDark
                                                text: slider.value.toFixed(delegateRoot.paramDecimals)
                                            }
                                        }
                                    }

                                    Component {
                                        id: checkEditorComponent

                                        QuiCheckBox {
                                            anchors.left: parent.left
                                            anchors.verticalCenter: parent.verticalCenter
                                            enabled: delegateRoot.paramEnabled
                                            checked: Utils.boolValue(delegateRoot.paramValue, false)
                                            text: ""
                                            onToggled: delegateRoot.commitValue(checked)
                                        }
                                    }

                                    Component {
                                        id: comboEditorComponent

                                        QuiComboBox {
                                            anchors.fill: parent
                                            enabled: delegateRoot.paramEnabled
                                            model: delegateRoot.paramOptions || []
                                            currentIndex: Math.max(0, delegateRoot.optionIndexForValue(delegateRoot.paramValue))
                                            onActivated: delegateRoot.commitValue(delegateRoot.optionValue(currentText))
                                        }
                                    }

                                    Component {
                                        id: textEditorComponent

                                        QuiTextField {
                                            anchors.fill: parent
                                            enabled: delegateRoot.paramEnabled
                                            text: Utils.stringValue(delegateRoot.paramValue)
                                            onEditingFinished: delegateRoot.commitValue(text)
                                        }
                                    }

                                    Component {
                                        id: weightComboEditorComponent

                                        QuiGroupComboBox {
                                            id: combo
                                            anchors.fill: parent
                                            enabled: delegateRoot.paramEnabled
                                            property var watchedValue: delegateRoot.paramValue
                                            property var watchedOptions: delegateRoot.paramOptions
                                            property string resolvedSavedValue: ""
                                            savedValue: resolvedSavedValue

                                            Component.onCompleted: {
                                                refreshCheckpointOptions()
                                            }

                                            function valueForSelection(value, groups) {
                                                const raw = collapseDuplicateLabel(value)
                                                for (let i = 0; i < groups.length; ++i) {
                                                    const group = groups[i]
                                                    const groupValue = group && group.value !== undefined ? String(group.value) : ""
                                                    if (groupValue === raw)
                                                        return raw
                                                    const subs = group && group.subOptions ? group.subOptions : []
                                                    for (let j = 0; j < subs.length; ++j) {
                                                        const sub = subs[j]
                                                        const subValue = sub && sub.value !== undefined ? String(sub.value) : ""
                                                        const label = String(group.label) + " / " + String(sub.label)
                                                        if (subValue === raw || label === raw
                                                                || stripExtension(label) === stripExtension(raw))
                                                            return subValue
                                                    }
                                                }
                                                for (let i = 0; i < groups.length; ++i) {
                                                    const group = groups[i]
                                                    const groupValue = group && group.value !== undefined ? String(group.value) : ""
                                                    if (groupValue.length > 0 && stripExtension(raw) === stripExtension(groupValue))
                                                        return groupValue
                                                }
                                                return raw
                                            }

                                            function stripExtension(value) {
                                                const raw = String(value)
                                                const slash = Math.max(raw.lastIndexOf("/"), raw.lastIndexOf("\\"))
                                                const dot = raw.lastIndexOf(".")
                                                return dot > slash ? raw.substring(0, dot) : raw
                                            }

                                            function collapseDuplicateLabel(value) {
                                                const raw = value === undefined || value === null ? "" : String(value)
                                                const parts = raw.split(" / ")
                                                if (parts.length === 2 && parts[0] === parts[1])
                                                    return parts[0]
                                                if (parts.length > 2 && parts.length % 2 === 0) {
                                                    const half = parts.length / 2
                                                    let duplicated = true
                                                    for (let i = 0; i < half; ++i) {
                                                        if (parts[i] !== parts[i + half]) {
                                                            duplicated = false
                                                            break
                                                        }
                                                    }
                                                    if (duplicated)
                                                        return parts.slice(0, half).join(" / ")
                                                }
                                                return raw
                                            }

                                            function refreshCheckpointOptions() {
                                                const groups = groupModel ? (groupModel.nestedOptions(rowIndex) || []) : []
                                                optionGroups = groups
                                                resolvedSavedValue = valueForSelection(delegateRoot.paramValue, groups)
                                            }

                                            onWatchedValueChanged: {
                                                // 模型切换/联动值变化后同步选项，避免关闭状态下残留旧模型的选项导致显示空白。
                                                if (!combo.popup.opened)
                                                    refreshCheckpointOptions()
                                            }

                                            onWatchedOptionsChanged: {
                                                if (!combo.popup.opened)
                                                    refreshCheckpointOptions()
                                            }

                                            onAboutToOpen: {
                                                refreshCheckpointOptions()
                                            }

                                            onCommit: function(value) {
                                                resolvedSavedValue = value
                                                delegateRoot.commitValue(value)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

