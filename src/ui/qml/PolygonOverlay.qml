import QtQuick

/*
 * 通用多边形覆盖层。
 *
 * 坐标由调用方明确声明其 viewport 和源图尺寸，组件只负责按
 * PreserveAspectFit 的同一缩放规则绘制。检查页和模型评估缩略图共用这
 * 个映射逻辑，避免两处多边形显示产生偏移。
 */
Item {
    id: root

    property var polygons: []
    property var coordinateViewport: ({x: 0, y: 0, width: 0, height: 0, valid: false})
    property real sourceWidth: 0
    property real sourceHeight: 0
    property real paintedWidth: width
    property real paintedHeight: height
    property color strokeColor: "#ff5252"
    property real fillOpacity: 0.16
    property real lineWidth: 2
    property int paintCount: 0

    function requestOverlayPaint() {
        overlay.requestPaint()
        // Visibility, image painted size, and coordinate bindings can settle
        // in separate QML update passes. Repaint once after those bindings
        // have settled so a newly shown overlay is not left with a cleared
        // canvas from its hidden state.
        repaintTimer.restart()
    }

    // The overlay is hidden until the derived heatmap image is ready. A
    // Canvas request made while its parent is hidden may be consumed before
    // the first visible frame, so repaint when the overlay becomes visible.
    onVisibleChanged: {
        if (visible)
            requestOverlayPaint()
    }

    Timer {
        id: repaintTimer
        interval: 0
        repeat: false
        onTriggered: overlay.requestPaint()
    }

    function pointValue(point) {
        if (point instanceof Array && point.length >= 2)
            return {valid: true, x: Number(point[0]), y: Number(point[1])}
        if (point && point.x !== undefined && point.y !== undefined)
            return {valid: true, x: Number(point.x), y: Number(point.y)}
        return {valid: false, x: 0, y: 0}
    }

    function polygonValue(value) {
        if (value && value.points !== undefined)
            return value.points
        return value
    }

    function mapPoint(point, viewport, scale, offsetX, offsetY) {
        var value = root.pointValue(point)
        if (!value.valid || !isFinite(value.x) || !isFinite(value.y))
            return {valid: false, x: 0, y: 0}
        return {
            valid: true,
            x: offsetX + (value.x - Number(viewport.x || 0)) * scale,
            y: offsetY + (value.y - Number(viewport.y || 0)) * scale
        }
    }

    Canvas {
        id: overlay
        objectName: "polygonOverlayCanvas"
        anchors.fill: parent

        onPaint: {
            root.paintCount += 1
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var viewport = root.coordinateViewport || {}
            var viewportWidth = Number(viewport.width || 0)
            var viewportHeight = Number(viewport.height || 0)
            var sourceWidth = Number(root.sourceWidth || 0)
            var sourceHeight = Number(root.sourceHeight || 0)
            if (viewportWidth <= 0 || viewportHeight <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
                return

            var contentWidth = Number(root.paintedWidth || 0)
            var contentHeight = Number(root.paintedHeight || 0)
            if (contentWidth <= 0 || contentHeight <= 0)
                return
            var scale = Math.min(contentWidth / sourceWidth, contentHeight / sourceHeight)
            var offsetX = (width - contentWidth) / 2
            var offsetY = (height - contentHeight) / 2

            ctx.save()
            ctx.strokeStyle = String(root.strokeColor)
            ctx.fillStyle = String(root.strokeColor)
            ctx.lineWidth = Math.max(1, Number(root.lineWidth || 1))
            ctx.globalAlpha = Math.max(0, Math.min(1, Number(root.fillOpacity || 0)))

            var allPolygons = root.polygons || []
            for (var polygonIndex = 0; polygonIndex < allPolygons.length; ++polygonIndex) {
                var points = root.polygonValue(allPolygons[polygonIndex])
                if (!points || points.length < 3)
                    continue
                var first = root.mapPoint(points[0], viewport, scale, offsetX, offsetY)
                if (!first.valid)
                    continue
                ctx.beginPath()
                ctx.moveTo(first.x, first.y)
                var validCount = 1
                for (var pointIndex = 1; pointIndex < points.length; ++pointIndex) {
                    var mapped = root.mapPoint(points[pointIndex], viewport, scale, offsetX, offsetY)
                    if (mapped.valid) {
                        ctx.lineTo(mapped.x, mapped.y)
                        ++validCount
                    }
                }
                if (validCount < 3)
                    continue
                ctx.closePath()
                ctx.fill()
                ctx.globalAlpha = 1
                ctx.stroke()
                ctx.globalAlpha = Math.max(0, Math.min(1, Number(root.fillOpacity || 0)))
            }
            ctx.restore()
        }

        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    onPolygonsChanged: requestOverlayPaint()
    onCoordinateViewportChanged: requestOverlayPaint()
    onSourceWidthChanged: requestOverlayPaint()
    onSourceHeightChanged: requestOverlayPaint()
    onPaintedWidthChanged: requestOverlayPaint()
    onPaintedHeightChanged: requestOverlayPaint()
    onStrokeColorChanged: requestOverlayPaint()
    onFillOpacityChanged: requestOverlayPaint()
    onLineWidthChanged: requestOverlayPaint()
}
