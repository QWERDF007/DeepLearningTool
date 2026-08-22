import QtQuick
import QtQuick.Layouts

import dltool.ui
import quickui

Item {
    id: root

    property var fieldModel: null
    property bool framed: true
    property bool showSectionHeaders: true
    property int horizontalPadding: framed ? 12 : 0
    property int verticalPadding: framed ? 12 : 0
    property int rowSpacing: 0
    property bool editable: true

    enabled: editable

    Layout.fillWidth: true
    implicitHeight: fieldsColumn.implicitHeight + verticalPadding * 2

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: root.framed ? QuiColor.Primary : QuiColor.Transparent
        border.color: root.framed ? QuiColor.Border : QuiColor.Transparent
        visible: root.framed
    }

    ColumnLayout {
        id: fieldsColumn

        anchors.fill: parent
        anchors.leftMargin: root.horizontalPadding
        anchors.rightMargin: root.horizontalPadding
        anchors.topMargin: root.verticalPadding
        anchors.bottomMargin: root.verticalPadding
        spacing: root.rowSpacing

        Repeater {
            model: root.fieldModel

            ParameterFieldDelegate {
                fieldModel: root.fieldModel
                rowIndex: index
                showSectionHeaders: root.showSectionHeaders
                Layout.fillWidth: true
            }
        }
    }
}
