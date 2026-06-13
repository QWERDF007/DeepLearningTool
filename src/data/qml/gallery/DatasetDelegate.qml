import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

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
        QuiText {
            text: control.name
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            elide: Text.ElideRight
        }
        QuiProgressBar {
            textVisible: visualPosition > 0
            value: control.progress
            Layout.preferredWidth: 100
        }
        QuiText {
            text: control.stats
            Layout.preferredWidth: 80
        }

        // QuiTextIconButton {
        //     iconSource: QuiFontIcon.Picture
        //     text: "导入数据"
        //     implicitWidth: 24
        //     implicitHeight: 24
        //     iconSize: 20
        //     verticalPadding: 0
        //     horizontalPadding: 0
        // }
    }
}
