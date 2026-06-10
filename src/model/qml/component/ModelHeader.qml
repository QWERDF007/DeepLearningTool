import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Item {
    id: header
    property string text
    property bool addEnable: true
    signal addClicked()

    RowLayout {
        anchors.fill: parent
        DltText {
            text: header.text
            font: DltFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        DltTextIconButton {
            id: addBtn
            visible: addEnable
            iconSource: DltFontIcon.Add
            text: "添加模型"
            onClicked: header.addClicked()
        }
    }
}
