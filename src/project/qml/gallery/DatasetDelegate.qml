import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Item {
    id: control
    height: 24
    width: 200

    property string name: ""
    property string stats: ""
    property int dataset_id: -1
    property real progress: 0

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        spacing: 10
        DltText {
            text: control.name
            Layout.fillWidth: true
        }
        DltProgressBar {
            textVisible: visualPosition > 0
            value: control.progress
            Layout.preferredWidth: 100
        }
        DltText {
            text: control.stats
            Layout.preferredWidth: 64
        }

        // DltTextIconButton {
        //     iconSource: DltFontIcon.Picture
        //     text: "导入数据"
        //     implicitWidth: 24
        //     implicitHeight: 24
        //     iconSize: 20
        //     verticalPadding: 0
        //     horizontalPadding: 0
        // }
    }
}
