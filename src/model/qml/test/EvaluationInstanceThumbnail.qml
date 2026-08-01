import QtQuick

Item {
    id: control
    property var record: ({})
    onRecordChanged: boxes.requestPaint()

    function cropRectFor(image) {
        var crop = control.record.cropBounds || ({})
        if (Number(crop.width || 0) > 0 && Number(crop.height || 0) > 0)
            return Qt.rect(Number(crop.x || 0), Number(crop.y || 0),
                           Number(crop.width), Number(crop.height))
        return Qt.rect(0, 0, Number(image.sourceSize.width || 0), Number(image.sourceSize.height || 0))
    }

    function contentRect() {
        var width = Number(preview.paintedWidth || preview.width)
        var height = Number(preview.paintedHeight || preview.height)
        return ({x: (boxes.width - width) / 2, y: (boxes.height - height) / 2,
                 width: width, height: height})
    }

    Image {
        id: preview
        anchors.fill: parent
        anchors.margins: 2
        source: control.record.thumbnailUrl
                || ""
        // The provider already returns the cropped viewport.  Applying the
        // absolute crop again would offset the image a second time.
        sourceClipRect: control.record.thumbnailUrl
                        ? Qt.rect(0, 0, preview.sourceSize.width, preview.sourceSize.height)
                        : control.cropRectFor(preview)
        fillMode: Image.PreserveAspectFit
        asynchronous: true
    }

    // A mask is already a raster artifact in the standard protocol.  It is
    // clipped with the same absolute crop as the source image and then
    // scaled by the same PreserveAspectFit transform, so it stays registered
    // with GT/PRED polygon and bbox overlays.
    Image {
        id: gtMask
        anchors.fill: preview
        source: control.record.gtMaskUrl || ""
        sourceClipRect: control.cropRectFor(gtMask)
        fillMode: Image.PreserveAspectFit
        opacity: 0.28
        asynchronous: true
        visible: source.length > 0
    }

    Image {
        id: predMask
        anchors.fill: preview
        source: control.record.predMaskUrl || ""
        sourceClipRect: control.cropRectFor(predMask)
        fillMode: Image.PreserveAspectFit
        opacity: 0.28
        asynchronous: true
        visible: source.length > 0
    }

    Canvas {
        id: boxes
        anchors.fill: preview
        anchors.margins: 2
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var viewport = control.contentRect()

            function drawBox(value, color, dashed) {
                if (!value || Number(value.width || 0) <= 0 || Number(value.height || 0) <= 0)
                    return
                ctx.save()
                ctx.strokeStyle = color
                ctx.lineWidth = 2
                ctx.setLineDash(dashed ? [6, 4] : [])
                ctx.strokeRect(viewport.x + Number(value.x || 0) * viewport.width,
                               viewport.y + Number(value.y || 0) * viewport.height,
                               Number(value.width) * viewport.width,
                               Number(value.height) * viewport.height)
                ctx.restore()
            }

            function drawPolygon(points, color, dashed) {
                if (!points || points.length < 3)
                    return false
                ctx.save()
                ctx.strokeStyle = color
                ctx.lineWidth = 2
                ctx.setLineDash(dashed ? [6, 4] : [])
                ctx.beginPath()
                for (var i = 0; i < points.length; ++i) {
                    var point = points[i]
                    var x = Number(point && point.length !== undefined ? point[0] : point.x)
                    var y = Number(point && point.length !== undefined ? point[1] : point.y)
                    if (i === 0)
                        ctx.moveTo(viewport.x + x * viewport.width, viewport.y + y * viewport.height)
                    else
                        ctx.lineTo(viewport.x + x * viewport.width, viewport.y + y * viewport.height)
                }
                ctx.closePath()
                ctx.stroke()
                ctx.restore()
                return true
            }

            if (!drawPolygon(control.record.gtOverlayPoints,
                             control.record.gtClassColor || "#00e676", false))
                drawBox(control.record.gtOverlayBounds, control.record.gtClassColor || "#00e676", false)
            if (!drawPolygon(control.record.predOverlayPoints,
                             control.record.predClassColor || "#ff5252", true))
                drawBox(control.record.predOverlayBounds, control.record.predClassColor || "#ff5252", true)
        }
        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
}
