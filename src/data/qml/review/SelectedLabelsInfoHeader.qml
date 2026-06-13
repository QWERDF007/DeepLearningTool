import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Item {
    id: control
    signal clicked()
    RowLayout {
        anchors.fill: parent
        QuiText {
            text: "所选标注信息:"
            font: QuiFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        QuiTextIconButton {
            id: addBtn
            iconSource: QuiFontIcon.Clear
            text: "清除选中"
            onClicked: control.clicked()
        }
    }
}
