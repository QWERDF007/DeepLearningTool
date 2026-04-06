import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data

Item {
    id: control
    signal clicked()
    RowLayout {
        anchors.fill: parent
        DltText {
            text: "所选标注信息:"
            font: DltFont.Subtitle
        }
        Item {
            Layout.fillWidth: true
        }
        DltTextIconButton {
            id: addBtn
            iconSource: DltFontIcon.Clear
            text: "清除选中"
            onClicked: control.clicked()
        }
    }
}
