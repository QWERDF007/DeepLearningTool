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
                                    property string paramNameEn: modelValue("nameEn", "")
                                    property string paramLabel: modelValue("nameCn", paramNameEn)
                                    property string paramDescription: modelValue("description", "")
                                    property string paramControlType: modelValue("controlType", "text")
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
                                                    sourceComponent: delegateRoot.editorComponent(delegateRoot.paramControlType)
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
                                            currentIndex: Math.max(0, (delegateRoot.paramOptions || []).indexOf(Utils.stringValue(delegateRoot.paramValue)))
                                            onActivated: delegateRoot.commitValue(currentText)
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
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
