import QtQuick
import QtQuick.Controls

Repeater {
    id: repeater
    property real offsetX: 0
    property real offsetY: 0
    property real factor: 0
    delegate: Rectangle {
        x: repeater.offsetX + model.x * repeater.factor
        y: repeater.offsetY + model.y * repeater.factor
        width: model.width * repeater.factor
        height: model.height * repeater.factor
        color: "transparent"
        border.color: model.color
        border.width: 2
    }
}