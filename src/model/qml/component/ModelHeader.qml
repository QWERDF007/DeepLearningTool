import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

Item {
    id: header
    property string text
    property bool addEnable: true
    signal addClicked()

    RowLayout {
        anchors.fill: parent
        QuiText {
            text: header.text
            font: QuiFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        QuiTextIconButton {
            id: addBtn
            visible: addEnable
            iconSource: QuiFontIcon.Add
            text: "添加模型"
            onClicked: header.addClicked()
        }
    }
}
