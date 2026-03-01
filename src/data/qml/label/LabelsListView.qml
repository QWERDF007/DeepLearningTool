import QtQuick
import QtQuick.Controls

import dltool.ui

Repeater {
    id: repeater
    property real offsetX: 0
    property real offsetY: 0
    property real factor: 0
    delegate: Rectangle {
        property var data: model.data
        x: repeater.offsetX + data.x * repeater.factor
        y: repeater.offsetY + data.y * repeater.factor
        width: data.width * repeater.factor
        height: data.height * repeater.factor
        color: DltColor.Transparent
        border.color: model.color
        border.width: model.selected ? 3 : model.hovered ? 2 : 1
    }
}