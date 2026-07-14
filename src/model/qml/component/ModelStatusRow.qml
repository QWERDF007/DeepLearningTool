import QtQuick
import QtQuick.Layouts

import dltool.ui
import quickui

RowLayout {
    id: control

    property string label: ""
    property string value: ""

    Layout.fillWidth: true
    spacing: 6

    QuiText {
        Layout.fillWidth: true
        text: control.label + ":"
        color: QuiColor.FontDark
        elide: Text.ElideRight
    }

    QuiText {
        Layout.fillWidth: true
        text: control.value
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignRight
    }
}
