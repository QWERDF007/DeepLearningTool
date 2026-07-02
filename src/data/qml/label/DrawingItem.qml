import QtQuick
import QtQuick.Controls

import dltool.ui

Item {
    id: drawItem
    anchors.fill: parent
    visible: false

    property int labelId: -1

    property real offsetX: 0
    property real offsetY: 0
    property real factor: 1

    property real ix: 0
    property real iy: 0
    property real iwidth: 0
    property real iheight: 0
    property var polygonPoints: []
    property color strokeColor: "red"
    property real fillOpacity: 0.3

    onFillOpacityChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true

        onPaint: {
            let ctx = getContext("2d")
            drawItem.paint(ctx)
        }
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

    function boundsFromPoints(points) {
        if (!points || points.length === 0) {
            return {x: 0, y: 0, width: 0, height: 0}
        }

        let xMin = points[0].x
        let yMin = points[0].y
        let xMax = points[0].x
        let yMax = points[0].y
        for (let point of points) {
            xMin = Math.min(xMin, point.x)
            yMin = Math.min(yMin, point.y)
            xMax = Math.max(xMax, point.x)
            yMax = Math.max(yMax, point.y)
        }
        return {x: xMin, y: yMin, width: xMax - xMin, height: yMax - yMin}
    }

    function applyData(data) {
        drawItem.visible = true
        drawItem.labelId = data.label_id ?? -1
        drawItem.strokeColor = data.color ?? "red"
        drawItem.polygonPoints = clonePoints(data.points ?? [])

        if (drawItem.polygonPoints.length > 0) {
            let bounds = boundsFromPoints(drawItem.polygonPoints)
            drawItem.ix = bounds.x
            drawItem.iy = bounds.y
            drawItem.iwidth = bounds.width
            drawItem.iheight = bounds.height
        } else {
            drawItem.ix = data.x ?? 0
            drawItem.iy = data.y ?? 0
            drawItem.iwidth = data.width ?? 0
            drawItem.iheight = data.height ?? 0
        }

        canvas.requestPaint()
    }

    function initItem(data) {
        applyData(data)
    }

    function updateItem(data) {
        applyData(data)
    }

    function clearItem() {
        drawItem.visible = false
        drawItem.labelId = -1
        drawItem.ix = 0
        drawItem.iy = 0
        drawItem.iwidth = 0
        drawItem.iheight = 0
        drawItem.polygonPoints = []
        canvas.requestPaint()
    }

    function getData() {
        let data = {
            label_id: drawItem.labelId,
            x: drawItem.ix,
            y: drawItem.iy,
            width: drawItem.iwidth,
            height: drawItem.iheight,
            color: drawItem.strokeColor
        }
        if (drawItem.polygonPoints.length > 0) {
            data.points = clonePoints(drawItem.polygonPoints)
            data.point_count = drawItem.polygonPoints.length
        }
        return data
    }

    function toScreen(point) {
        return Qt.point(drawItem.offsetX + point.x * drawItem.factor,
                        drawItem.offsetY + point.y * drawItem.factor)
    }

    function paint(ctx) {
        ctx.clearRect(0, 0, canvas.width, canvas.height)
        if (!drawItem.visible) {
            return
        }

        ctx.save()
        ctx.strokeStyle = drawItem.strokeColor
        ctx.fillStyle = drawItem.strokeColor
        ctx.lineWidth = 2

        if (drawItem.polygonPoints.length > 0) {
            let first = toScreen(drawItem.polygonPoints[0])
            ctx.beginPath()
            ctx.moveTo(first.x, first.y)
            for (let i = 1; i < drawItem.polygonPoints.length; ++i) {
                let point = toScreen(drawItem.polygonPoints[i])
                ctx.lineTo(point.x, point.y)
            }
            if (drawItem.polygonPoints.length >= 3) {
                ctx.closePath()
                ctx.globalAlpha = Math.max(0, Math.min(1, drawItem.fillOpacity))
                ctx.fill()
                ctx.globalAlpha = 1
            }
            ctx.stroke()

            for (let point of drawItem.polygonPoints) {
                let screenPoint = toScreen(point)
                ctx.fillRect(screenPoint.x - 3, screenPoint.y - 3, 6, 6)
            }
        } else if (drawItem.iwidth > 0 || drawItem.iheight > 0) {
            ctx.globalAlpha = Math.max(0, Math.min(1, drawItem.fillOpacity))
            ctx.fillRect(drawItem.offsetX + drawItem.ix * drawItem.factor,
                         drawItem.offsetY + drawItem.iy * drawItem.factor,
                         drawItem.iwidth * drawItem.factor,
                         drawItem.iheight * drawItem.factor)
            ctx.globalAlpha = 1
            ctx.strokeRect(drawItem.offsetX + drawItem.ix * drawItem.factor,
                           drawItem.offsetY + drawItem.iy * drawItem.factor,
                           drawItem.iwidth * drawItem.factor,
                           drawItem.iheight * drawItem.factor)
        }

        ctx.restore()
    }
}
