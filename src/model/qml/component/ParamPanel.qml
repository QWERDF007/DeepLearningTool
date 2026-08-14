import QtQuick
import QtQuick.Layouts

import dltool.model
import dltool.ui
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
                    if (typeof model !== "undefined" && model
                            && model[roleName] !== undefined && model[roleName] !== null) {
                        return model[roleName]
                    }
                    return fallbackValue
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

                            ParameterFieldsPanel {
                                Layout.fillWidth: true
                                fieldModel: groupRoot.groupModel
                                framed: false
                                showSectionHeaders: true
                            }
                        }
                    }
                }
            }
        }
    }
}
