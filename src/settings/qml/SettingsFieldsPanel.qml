import QtQuick
import QtQuick.Layouts

import dltool.ui
import quickui

Rectangle {
    id: root

    property var fieldModel: null

    Layout.fillWidth: true

    implicitHeight: fieldsColumn.implicitHeight + 24
    radius: 4
    color: QuiColor.Primary
    border.color: QuiColor.Border
    clip: true

    ColumnLayout {
        id: fieldsColumn

        anchors.fill: parent
        anchors.margins: 12
        spacing: 0

        Repeater {
            model: root.fieldModel

            SettingsFieldDelegate {
                fieldModel: root.fieldModel
                Layout.fillWidth: true
            }
        }
    }
}
