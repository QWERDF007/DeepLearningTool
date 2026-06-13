import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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

                property int groupPartIndex: Number(groupRole("partIndex", 0))
                property var groupModel: groupRole("groupModel", null)
                property string groupLabel: groupRole("label", groupRole("key", ""))
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
                                color: QuiColor.FontPrimary
                                font: QuiFont.Title
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
                                Layout.preferredHeight: Math.max(contentHeight, 44)
                                spacing: control.partSpacing
                                interactive: false
                                clip: true
                                model: groupRoot.groupModel

                                delegate: Item {
                                    id: delegateRoot

                                    width: ListView.view ? ListView.view.width : 0
                                    height: paramDescription.length > 0 ? 64 : 50

                                    property var groupModel: groupRoot.groupModel
                                    property int rowIndex: index
                                    property real labelWidth: Math.max(0, Math.floor(width / 3))
                                    property string paramKey: modelValue("key", "")
                                    property string paramLabel: modelValue("label", paramKey)
                                    property string paramDescription: modelValue("description", "")
                                    property string paramEditorType: modelValue("editorType", "text")
                                    property var paramValue: modelValue("value", modelValue("defaultValue", ""))
                                    property var paramDefaultValue: modelValue("defaultValue", "")
                                    property var paramMinimumValue: modelValue("minimumValue", 0)
                                    property var paramMaximumValue: modelValue("maximumValue", 100)
                                    property var paramStepValue: modelValue("stepValue", 1)
                                    property int paramDecimals: Number(modelValue("decimals", 0))
                                    property bool paramEnabled: Boolean(modelValue("enabled", true))
                                    property var paramOptions: modelValue("options", [])
                                    property string paramUnit: modelValue("unit", "")

                                    function modelValue(roleName, fallbackValue) {
                                        if (typeof model !== "undefined" && model && model[roleName] !== undefined && model[roleName] !== null) {
                                            return model[roleName];
                                        }
                                        return fallbackValue;
                                    }

                                    function numberValue(value, fallbackValue) {
                                        const result = Number(value);
                                        return isNaN(result) ? fallbackValue : result;
                                    }

                                    function stringValue(value) {
                                        if (value === undefined || value === null) {
                                            return "";
                                        }
                                        return String(value);
                                    }

                                    function commitValue(value) {
                                        if (groupModel && rowIndex >= 0) {
                                            groupModel.setValue(rowIndex, value);
                                        }
                                    }

                                    function editorComponent(type) {
                                        if (type === "integer" || type === "double") {
                                            return spinEditorComponent;
                                        }
                                        if (type === "slider") {
                                            return sliderEditorComponent;
                                        }
                                        if (type === "checkbox") {
                                            return checkEditorComponent;
                                        }
                                        if (type === "comboBox") {
                                            return comboEditorComponent;
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

                                            QuiText {
                                                Layout.fillWidth: true
                                                text: delegateRoot.paramLabel
                                                color: delegateRoot.paramEnabled ? QuiColor.FontPrimary : QuiColor.FontDark
                                                elide: Text.ElideRight
                                            }

                                            QuiText {
                                                Layout.fillWidth: true
                                                text: delegateRoot.paramDescription
                                                visible: text.length > 0
                                                color: QuiColor.FontDark
                                                font: QuiFont.Caption
                                                elide: Text.ElideRight
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
                                                    sourceComponent: delegateRoot.editorComponent(delegateRoot.paramEditorType)
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
                                            value: delegateRoot.numberValue(delegateRoot.paramValue, delegateRoot.numberValue(delegateRoot.paramDefaultValue, 0))
                                            minValue: delegateRoot.numberValue(delegateRoot.paramMinimumValue, 0)
                                            maxValue: delegateRoot.numberValue(delegateRoot.paramMaximumValue, 10000)
                                            step: delegateRoot.numberValue(delegateRoot.paramStepValue, 1)
                                            decimals: delegateRoot.paramEditorType === "double" ? delegateRoot.paramDecimals : 0
                                            onEditingFinished: {
                                                const nextValue = delegateRoot.paramEditorType === "integer" ? Math.round(value) : value;
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
                                                from: delegateRoot.numberValue(delegateRoot.paramMinimumValue, 0)
                                                to: delegateRoot.numberValue(delegateRoot.paramMaximumValue, 1)
                                                value: delegateRoot.numberValue(delegateRoot.paramValue, delegateRoot.numberValue(delegateRoot.paramDefaultValue, 0))
                                                stepSize: delegateRoot.numberValue(delegateRoot.paramStepValue, 0.01)
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
                                            checked: Boolean(delegateRoot.paramValue)
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
                                            currentIndex: Math.max(0, (delegateRoot.paramOptions || []).indexOf(delegateRoot.stringValue(delegateRoot.paramValue)))
                                            onActivated: delegateRoot.commitValue(currentText)
                                        }
                                    }

                                    Component {
                                        id: textEditorComponent

                                        QuiTextField {
                                            anchors.fill: parent
                                            enabled: delegateRoot.paramEnabled
                                            text: delegateRoot.stringValue(delegateRoot.paramValue)
                                            onEditingFinished: delegateRoot.commitValue(text)
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
