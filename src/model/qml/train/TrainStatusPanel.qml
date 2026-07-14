import QtQuick
import QtQuick.Layouts

import dltool.ui
import dltool.model
import quickui

Rectangle {
    id: control

    property var stateData: ({})

    color: QuiColor.Primary
    border.color: QuiColor.Border
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        QuiText {
            Layout.fillWidth: true
            text: "训练状态"
            font: QuiFont.Subtitle
        }

        ModelStatusRow {
            label: "Epoch"
            value: control.stateData.epoch || "-"
        }
        ModelStatusRow {
            label: "Iter"
            value: control.stateData.iter || "-"
        }
        ModelStatusRow {
            label: "LR"
            value: control.stateData.lr || "-"
        }
        ModelStatusRow {
            label: "Loss"
            value: control.stateData.loss || "-"
        }
        ModelStatusRow {
            label: "Elapsed Time"
            value: control.stateData.elapsed || "-"
        }

        Item { Layout.fillHeight: true }
    }
}
