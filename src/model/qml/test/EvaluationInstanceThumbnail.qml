import QtQuick

Item {
    id: control
    property var record: ({})
    onRecordChanged: boxes.requestPaint()

    // 裁剪视口不再由评估预计算:按 LabelInstanceThumbnail 模式,
    // 从 GT/PRED 绝对 bounds 推导(并集 + 5% 边距,钳制到图像边界),
    // 与 provider 渲染时的裁剪公式保持一致。
    function viewportRect() {
        var gt = control.record.gtBounds || {}
        var pd = control.record.predBounds || {}
        var gtv = Number(gt.width || 0) > 0 && Number(gt.height || 0) > 0
        var pdv = Number(pd.width || 0) > 0 && Number(pd.height || 0) > 0
        if (!gtv && !pdv)
            return { valid: false }
        var left, top, right, bottom
        if (gtv && pdv) {
            left = Math.min(Number(gt.x), Number(pd.x))
            top = Math.min(Number(gt.y), Number(pd.y))
            right = Math.max(Number(gt.x) + Number(gt.width), Number(pd.x) + Number(pd.width))
            bottom = Math.max(Number(gt.y) + Number(gt.height), Number(pd.y) + Number(pd.height))
        } else if (gtv) {
            left = Number(gt.x); top = Number(gt.y)
            right = left + Number(gt.width); bottom = top + Number(gt.height)
        } else {
            left = Number(pd.x); top = Number(pd.y)
            right = left + Number(pd.width); bottom = top + Number(pd.height)
        }
        var pad = Math.max(4.0, Math.max(right - left, bottom - top) * 0.05)
        var iw = Number(control.record.imageWidth || 0)
        var ih = Number(control.record.imageHeight || 0)
        var vRight = Math.min(right + pad, iw > 0 ? iw : right + pad)
        var vBottom = Math.min(bottom + pad, ih > 0 ? ih : bottom + pad)
        var vLeft = Math.min(Math.max(0, left - pad), vRight)
        var vTop = Math.min(Math.max(0, top - pad), vBottom)
        if (vRight - vLeft <= 0 || vBottom - vTop <= 0)
            return { valid: false }
        return { valid: true, x: vLeft, y: vTop, width: vRight - vLeft, height: vBottom - vTop }
    }

    function cropRectFor(image) {
        var viewport = control.viewportRect()
        if (viewport.valid)
            return Qt.rect(viewport.x, viewport.y, viewport.width, viewport.height)
        return Qt.rect(0, 0, Number(image.sourceSize.width || 0), Number(image.sourceSize.height || 0))
    }

    function contentRect() {
        var width = Number(preview.paintedWidth || preview.width)
        var height = Number(preview.paintedHeight || preview.height)
        return ({x: (boxes.width - width) / 2, y: (boxes.height - height) / 2,
                 width: width, height: height})
    }

    // 视口内坐标 → 画布像素坐标,与 LabelInstanceThumbnail 的
    // mapLabelPointToThumbnail 同款换算。
    function mapViewportPoint(viewpoint, vp, scale, offsetX, offsetY) {
        return {
            x: offsetX + (viewpoint.x - vp.x) * scale,
            y: offsetY + (viewpoint.y - vp.y) * scale
        }
    }

    function overlayScale() {
        var vp = control.viewportRect()
        if (!vp.valid || vp.width <= 0 || vp.height <= 0)
            return 0
        var viewport = control.contentRect()
        return Math.min(viewport.width / vp.width, viewport.height / vp.height)
    }

    function overlayOffset() {
        return control.contentRect()
    }

    function polygonPoints(geometry) {
        var result = []
        if (!geometry || !geometry.points)
            return result
        for (var i = 0; i < geometry.points.length; ++i) {
            var point = geometry.points[i]
            result.push({x: Number(point[0]), y: Number(point[1])})
        }
        return result
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
    // clipped with the same viewport rect as the provider's crop and then
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
            var vp = control.viewportRect()
            if (!vp.valid)
                return
            var viewport = control.contentRect()
            var scale = Math.min(viewport.width / vp.width, viewport.height / vp.height)

            function toPixels(value) {
                return control.mapViewportPoint(value, vp, scale, viewport.x, viewport.y)
            }

            function drawBox(value, color, dashed) {
                if (!value || Number(value.width || 0) <= 0 || Number(value.height || 0) <= 0)
                    return
                var topLeft = toPixels({x: Number(value.x), y: Number(value.y)})
                ctx.save()
                ctx.strokeStyle = color
                ctx.lineWidth = 2
                ctx.setLineDash(dashed ? [6, 4] : [])
                ctx.strokeRect(topLeft.x, topLeft.y,
                               Number(value.width) * scale,
                               Number(value.height) * scale)
                ctx.restore()
            }

            function drawPolygon(geometry, color, dashed) {
                var points = control.polygonPoints(geometry)
                if (points.length < 3)
                    return false
                ctx.save()
                ctx.strokeStyle = color
                ctx.lineWidth = 2
                ctx.setLineDash(dashed ? [6, 4] : [])
                ctx.beginPath()
                for (var i = 0; i < points.length; ++i) {
                    var p = toPixels(points[i])
                    if (i === 0)
                        ctx.moveTo(p.x, p.y)
                    else
                        ctx.lineTo(p.x, p.y)
                }
                ctx.closePath()
                ctx.stroke()
                ctx.restore()
                return true
            }

            if (!drawPolygon(control.record.gtGeometry, control.record.gtClassColor || "#00e676", false))
                drawBox(control.record.gtBounds, control.record.gtClassColor || "#00e676", false)
            if (!drawPolygon(control.record.predGeometry, control.record.predClassColor || "#ff5252", true))
                drawBox(control.record.predBounds, control.record.predClassColor || "#ff5252", true)
        }
        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
}
