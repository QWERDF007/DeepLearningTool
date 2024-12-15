import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Item {
    id: control
    height: 32
    width: 200

    property string name: ""
    property string stats: ""

    RowLayout {
        id: layout
        anchors.fill: parent
        spacing: 10
        DltText {
            text: control.name
            Layout.fillWidth: true
        }
        Rectangle {
            height: 12
            Layout.preferredWidth: 100
        }
        DltText {
            text: control.stats
            Layout.preferredWidth: 48
        }

        DltTextIconButton {
            iconSource: DltFontIcon.ImportMirrored
            text: "导入数据"
            implicitWidth: 20
            implicitHeight: 20
            iconSize: 16
            verticalPadding: 0
            horizontalPadding: 0
        }
    }
}
