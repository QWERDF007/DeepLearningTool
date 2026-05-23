import QtQuick
import QtQuick.Controls

import dltool.ui

Repeater {
    id: repeater
    property real offsetX: 0
    property real offsetY: 0
    property real factor: 0

    delegate: Item {
        id: labelDelegate
        anchors.fill: parent

        property var labelData: model.data
        property color labelColor: model.color
        property bool labelSelected: model.selected ?? false
        property bool labelHovered: model.hovered ?? false

        Canvas {
            id: canvas
            anchors.fill: parent
            antialiasing: true

            onPaint: {
                let ctx = getContext("2d")
                labelDelegate.paint(ctx)
            }
        }

        onLabelDataChanged: canvas.requestPaint()
        onLabelColorChanged: canvas.requestPaint()
        onLabelSelectedChanged: canvas.requestPaint()
        onLabelHoveredChanged: canvas.requestPaint()

        Connections {
            target: repeater
            function onOffsetXChanged() { canvas.requestPaint() }
            function onOffsetYChanged() { canvas.requestPaint() }
            function onFactorChanged() { canvas.requestPaint() }
        }

        function clonePoints(points) {
            let result = []
            if (!points) {
                return result
            }
            for (let point of points) {
                result.push({x: point.x, y: point.y})
            }
            return result
        }

        function toScreen(point) {
            return Qt.point(repeater.offsetX + point.x * repeater.factor,
                            repeater.offsetY + point.y * repeater.factor)
        }

        function paint(ctx) {
            ctx.clearRect(0, 0, canvas.width, canvas.height)
            if (!labelData) {
                return
            }

            let lineWidth = labelSelected ? 3 : labelHovered ? 2 : 1
            let points = clonePoints(labelData.points ?? [])

            ctx.save()
            ctx.strokeStyle = labelColor
            ctx.fillStyle = labelColor
            ctx.lineWidth = lineWidth

            if (points.length >= 3) {
                let first = toScreen(points[0])
                ctx.beginPath()
                ctx.moveTo(first.x, first.y)
                for (let i = 1; i < points.length; ++i) {
                    let point = toScreen(points[i])
                    ctx.lineTo(point.x, point.y)
                }
                ctx.closePath()
                ctx.globalAlpha = labelSelected || labelHovered ? 0.18 : 0.1
                ctx.fill()
                ctx.globalAlpha = 1
                ctx.stroke()

                if (labelSelected) {
                    for (let point of points) {
                        let screenPoint = toScreen(point)
                        ctx.fillRect(screenPoint.x - 3, screenPoint.y - 3, 6, 6)
                    }
                }
            } else {
                ctx.strokeRect(repeater.offsetX + labelData.x * repeater.factor,
                               repeater.offsetY + labelData.y * repeater.factor,
                               labelData.width * repeater.factor,
                               labelData.height * repeater.factor)
            }

            ctx.restore()
        }
    }
}
