import QtQuick

Repeater {
    id: repeater
    property real offsetX: 0
    property real offsetY: 0
    property real factor: 0
    property bool showBoundingBoxes: false
    property real fillOpacity: 0.3

    delegate: Item {
        id: labelDelegate

        property var labelData: model.data
        property color labelColor: model.color
        property bool labelSelected: model.selected ?? false
        property bool labelHovered: model.hovered ?? false
        property real bboxX: labelData ? (labelData.x ?? 0) : 0
        property real bboxY: labelData ? (labelData.y ?? 0) : 0
        property real bboxWidth: labelData ? (labelData.width ?? 0) : 0
        property real bboxHeight: labelData ? (labelData.height ?? 0) : 0
        property real screenMargin: labelSelected ? 8 : labelHovered ? 6 : 4
        property real safeFactor: Math.max(repeater.factor, 0.0001)
        property real imageMargin: screenMargin / safeFactor
        property real rawLeft: bboxX - imageMargin
        property real rawTop: bboxY - imageMargin
        property real rawRight: bboxX + bboxWidth + imageMargin
        property real rawBottom: bboxY + bboxHeight + imageMargin
        property real viewportLeft: -repeater.offsetX / safeFactor
        property real viewportTop: -repeater.offsetY / safeFactor
        property real viewportRight: viewportLeft + (parent ? parent.width : 0) / safeFactor
        property real viewportBottom: viewportTop + (parent ? parent.height : 0) / safeFactor
        property real visibleLeft: Math.max(rawLeft, viewportLeft)
        property real visibleTop: Math.max(rawTop, viewportTop)
        property real visibleRight: Math.min(rawRight, viewportRight)
        property real visibleBottom: Math.min(rawBottom, viewportBottom)
        x: repeater.offsetX + visibleLeft * safeFactor
        y: repeater.offsetY + visibleTop * safeFactor
        width: Math.max(1, (visibleRight - visibleLeft) * safeFactor)
        height: Math.max(1, (visibleBottom - visibleTop) * safeFactor)
        visible: labelData !== undefined && labelData !== null
                 && visibleRight > visibleLeft && visibleBottom > visibleTop

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
        onWidthChanged: canvas.requestPaint()
        onHeightChanged: canvas.requestPaint()
        onVisibleLeftChanged: canvas.requestPaint()
        onVisibleTopChanged: canvas.requestPaint()

        Connections {
            target: repeater
            function onFactorChanged() { canvas.requestPaint() }
            function onShowBoundingBoxesChanged() { canvas.requestPaint() }
            function onFillOpacityChanged() { canvas.requestPaint() }
        }

        function toScreen(point) {
            return Qt.point((point.x - labelDelegate.visibleLeft) * labelDelegate.safeFactor,
                            (point.y - labelDelegate.visibleTop) * labelDelegate.safeFactor)
        }

        function paint(ctx) {
            ctx.clearRect(0, 0, canvas.width, canvas.height)
            if (!labelData) {
                return
            }

            let lineWidth = labelSelected ? 3 : labelHovered ? 2 : 1
            let points = labelData.points ?? []

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
                ctx.globalAlpha = Math.max(0, Math.min(1, repeater.fillOpacity))
                ctx.fill()
                ctx.globalAlpha = 1
                ctx.stroke()

                if (labelSelected) {
                    let handleSize = 6
                    for (let point of points) {
                        let screenPoint = toScreen(point)
                        ctx.fillRect(screenPoint.x - handleSize / 2,
                                     screenPoint.y - handleSize / 2,
                                     handleSize,
                                     handleSize)
                    }
                }

                if (repeater.showBoundingBoxes) {
                    ctx.save()
                    ctx.setLineDash([6, 4])
                    ctx.globalAlpha = labelSelected || labelHovered ? 0.9 : 0.65
                    ctx.strokeRect((labelData.x - labelDelegate.visibleLeft) * labelDelegate.safeFactor,
                                   (labelData.y - labelDelegate.visibleTop) * labelDelegate.safeFactor,
                                   labelData.width * labelDelegate.safeFactor,
                                   labelData.height * labelDelegate.safeFactor)
                    ctx.restore()
                }
            } else {
                ctx.globalAlpha = Math.max(0, Math.min(1, repeater.fillOpacity))
                ctx.fillRect((labelData.x - labelDelegate.visibleLeft) * labelDelegate.safeFactor,
                             (labelData.y - labelDelegate.visibleTop) * labelDelegate.safeFactor,
                             labelData.width * labelDelegate.safeFactor,
                             labelData.height * labelDelegate.safeFactor)
                ctx.globalAlpha = 1
                ctx.strokeRect((labelData.x - labelDelegate.visibleLeft) * labelDelegate.safeFactor,
                               (labelData.y - labelDelegate.visibleTop) * labelDelegate.safeFactor,
                               labelData.width * labelDelegate.safeFactor,
                               labelData.height * labelDelegate.safeFactor)
            }

            ctx.restore()
        }
    }
}
