import QtQuick
import QtQuick.Layouts

import dltool.ui
import quickui

Rectangle {
    id: control

    property string metricText: ""

    color: QuiColor.Primary
    border.color: QuiColor.Border
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        QuiText {
            Layout.fillWidth: true
            text: "评估状态"
            font: QuiFont.Subtitle
        }

        QuiText {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: "暂无评估指标"
            color: QuiColor.FontDark
            visible: control.metricText.length === 0
        }

        QuiText {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: control.metricText
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignTop
            visible: control.metricText.length > 0
        }
    }
}
