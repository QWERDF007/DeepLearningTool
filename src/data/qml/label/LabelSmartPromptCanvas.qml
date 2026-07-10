import QtQuick

import dltool.data

Canvas {
    id: promptCanvas

    property bool active: false
    property var geometry: null
    property var points: []
    property var hoverPoint: ({})
    property bool hoverPointValid: false
    property var box: ({})
    property bool boxValid: false
    readonly property color positiveColor: "#20B15A"
    readonly property color negativeColor: "#E5484D"

    antialiasing: true
    visible: active && (points.length > 0 || hoverPointValid || boxValid)

    onPaint: {
        let ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        if (!visible || !geometry) {
            return
        }

        ctx.save()
        if (boxValid) {
            paintPromptBox(ctx, box)
        }
        for (let point of points) {
            paintPromptPoint(ctx,
                             point,
                             point.label === LabelCanvasEnums.ForegroundPrompt ? positiveColor : negativeColor,
                             1)
        }
        if (hoverPointValid) {
            paintPromptPoint(ctx, hoverPoint, positiveColor, 0.65)
        }
        ctx.restore()
    }

    onVisibleChanged: requestPaint()
    onPointsChanged: requestPaint()
    onHoverPointChanged: requestPaint()
    onHoverPointValidChanged: requestPaint()
    onBoxChanged: requestPaint()
    onBoxValidChanged: requestPaint()

    function paintPromptBox(ctx, promptBox) {
        let topLeft = geometry.toScreen({x: promptBox.x, y: promptBox.y})
        let bottomRight = geometry.toScreen({x: promptBox.x + promptBox.width,
                                             y: promptBox.y + promptBox.height})
        ctx.globalAlpha = 1
        ctx.lineWidth = 2
        ctx.strokeStyle = positiveColor
        ctx.setLineDash([7, 4])
        ctx.strokeRect(topLeft.x, topLeft.y,
                       bottomRight.x - topLeft.x, bottomRight.y - topLeft.y)
        ctx.setLineDash([])
    }

    function paintPromptPoint(ctx, point, fillColor, alpha) {
        let screen = geometry.toScreen(point)
        ctx.beginPath()
        ctx.arc(screen.x, screen.y, 6, 0, Math.PI * 2)
        ctx.fillStyle = fillColor
        ctx.globalAlpha = alpha
        ctx.fill()
        ctx.globalAlpha = 1
        ctx.lineWidth = 2
        ctx.strokeStyle = "white"
        ctx.stroke()
    }
}
