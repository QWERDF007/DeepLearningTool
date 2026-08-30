import QtQuick
import QtQuick.Controls

import dltool.ui

Item {
    id: control
    property var record: ({})
    property bool heatmapEnabled: false
    property real heatmapThreshold: 1.0
    property bool heatmapFailed: false
    property bool heatmapReady: false
    property bool heatmapLoading: false
    property bool keepOriginalWhileLoading: false
    property string activeHeatmapUrl: ""
    readonly property bool heatmapAvailable: control.heatmapEnabled
                                           && String(control.record.heatmapUrl || "").length > 0
    readonly property bool heatmapMode: control.heatmapAvailable
                                       && control.heatmapReady
                                       && !control.heatmapFailed

    function beginHeatmapLoad() {
        var nextUrl = String(control.record.heatmapUrl || "")
        if (!control.heatmapEnabled || nextUrl.length === 0) {
            control.activeHeatmapUrl = nextUrl
            control.heatmapReady = false
            control.heatmapLoading = false
            control.heatmapFailed = false
            control.keepOriginalWhileLoading = false
            return
        }
        if (nextUrl === control.activeHeatmapUrl && !control.heatmapFailed
                && (control.heatmapReady || control.heatmapLoading))
            return

        // Keep the already rendered original image visible until the derived
        // image has completed.  The first enable also needs this fallback.
        var preserve = true
        control.activeHeatmapUrl = nextUrl
        control.heatmapReady = false
        control.heatmapLoading = true
        control.heatmapFailed = false
        control.keepOriginalWhileLoading = preserve
    }

    function requestOverlayPaint() {
        if (boxes)
            boxes.requestPaint()
        if (anomalyOverlay)
            anomalyOverlay.requestOverlayPaint()
    }

    function scheduleOverlayPaint() {
        control.requestOverlayPaint()
        overlayPaintTimer.restart()
    }

    Timer {
        id: overlayPaintTimer
        interval: 0
        repeat: false
        onTriggered: control.requestOverlayPaint()
    }

    onRecordChanged: {
        beginHeatmapLoad()
        requestOverlayPaint()
    }
    onHeatmapEnabledChanged: {
        if (heatmapEnabled)
            beginHeatmapLoad()
        else {
            // Invalidate a late Image status from the previous source.
            control.activeHeatmapUrl = ""
            heatmapReady = false
            heatmapLoading = false
            heatmapFailed = false
            keepOriginalWhileLoading = false
        }
        requestOverlayPaint()
    }
    onHeatmapThresholdChanged: {
        beginHeatmapLoad()
        requestOverlayPaint()
    }
    onHeatmapModeChanged: scheduleOverlayPaint()
    onHeatmapReadyChanged: scheduleOverlayPaint()

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

        // EvaluationThumbnailImageProvider crops with floor(left/top) and
        // ceil(right/bottom). Keep QML's source viewport identical to that
        // integer crop so original-coordinate polygons do not drift.
        var leftPixel = Math.max(0, Math.floor(vLeft))
        var topPixel = Math.max(0, Math.floor(vTop))
        var rightPixel = Math.max(leftPixel, Math.ceil(vRight))
        var bottomPixel = Math.max(topPixel, Math.ceil(vBottom))
        if (iw > 0)
            rightPixel = Math.min(iw, rightPixel)
        if (ih > 0)
            bottomPixel = Math.min(ih, bottomPixel)
        if (rightPixel <= leftPixel || bottomPixel <= topPixel)
            return { valid: false }
        return { valid: true, x: leftPixel, y: topPixel,
                 width: rightPixel - leftPixel, height: bottomPixel - topPixel }
    }

    function cropRectFor(image) {
        var viewport = control.viewportRect()
        if (viewport.valid)
            return Qt.rect(viewport.x, viewport.y, viewport.width, viewport.height)
        return Qt.rect(0, 0, Number(image.sourceSize.width || 0), Number(image.sourceSize.height || 0))
    }

    function contentRect() {
        var width = Number(preview.paintedWidth || 0)
        var height = Number(preview.paintedHeight || 0)
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
        objectName: "originalPreview"
        anchors.fill: parent
        anchors.margins: 2
        visible: !control.heatmapMode || control.keepOriginalWhileLoading
        source: control.record.thumbnailUrl || ""
        // The provider already returns the cropped viewport.  Applying the
        // absolute crop again would offset the image a second time.
        sourceClipRect: control.record.thumbnailUrl
                        ? Qt.rect(0, 0, preview.sourceSize.width, preview.sourceSize.height)
                        : control.cropRectFor(preview)
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        onStatusChanged: control.requestOverlayPaint()
        onPaintedWidthChanged: control.requestOverlayPaint()
        onPaintedHeightChanged: control.requestOverlayPaint()
        onSourceSizeChanged: control.requestOverlayPaint()
    }

    Image {
        id: heatmapPreview
        objectName: "heatmapPreview"
        anchors.fill: parent
        anchors.margins: 2
        // Do not expose a partially loaded derived image.  During loading the
        // original image and its original-coordinate overlay remain visible.
        visible: control.heatmapMode
        source: control.heatmapAvailable ? control.activeHeatmapUrl : ""
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        onSourceChanged: {
            if (!control.heatmapAvailable || String(source || "").length === 0)
                return
            control.heatmapLoading = true
            control.heatmapReady = false
            control.heatmapFailed = false
        }
        onStatusChanged: {
            // Image.source is a QML url while activeHeatmapUrl originates as
            // a C++ string. Comparing their text is not reliable for encoded
            // Windows paths. This Image is the single active request, so its
            // status is the authoritative lifecycle signal.
            if (!control.heatmapAvailable || String(source || "").length === 0)
                return
            if (status === Image.Loading) {
                control.heatmapLoading = true
            } else if (status === Image.Ready) {
                control.heatmapLoading = false
                control.heatmapReady = true
                control.heatmapFailed = false
                control.keepOriginalWhileLoading = false
            } else if (status === Image.Error) {
                control.heatmapLoading = false
                control.heatmapReady = false
                control.heatmapFailed = true
                control.keepOriginalWhileLoading = true
            }
            control.requestOverlayPaint()
        }
        onPaintedWidthChanged: control.requestOverlayPaint()
        onPaintedHeightChanged: control.requestOverlayPaint()
        onSourceSizeChanged: control.requestOverlayPaint()
    }

    BusyIndicator {
        objectName: "heatmapBusyIndicator"
        anchors.centerIn: parent
        running: control.heatmapAvailable && control.heatmapLoading && !control.heatmapFailed
                 && !control.heatmapMode
        visible: running
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
        visible: !control.heatmapMode && preview.visible && source.length > 0
    }

    Image {
        id: predMask
        anchors.fill: preview
        source: control.record.predMaskUrl || ""
        sourceClipRect: control.cropRectFor(predMask)
        fillMode: Image.PreserveAspectFit
        opacity: 0.28
        asynchronous: true
        visible: !control.heatmapMode && preview.visible && source.length > 0
    }

    Canvas {
        id: boxes
        anchors.fill: preview
        anchors.margins: 2
        visible: !control.heatmapMode && preview.visible && preview.status === Image.Ready
                 && preview.paintedWidth > 0 && preview.paintedHeight > 0
                 && control.viewportRect().valid
        onVisibleChanged: requestPaint()
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

    // 异常检测的所有连通区域共用检查页的多边形覆盖层。热力图开启时
    // 使用模型坐标；关闭时使用同一批区域的原图坐标。
    PolygonOverlay {
        id: anomalyOverlay
        objectName: "anomalyOverlay"
        // Keep one visible Canvas across mode changes. Re-anchoring or hiding
        // the Canvas while an asynchronous Image becomes ready can consume
        // its first repaint before the scene graph exposes it.
        anchors.fill: parent
        anchors.margins: 2
        z: 3
        visible: (control.heatmapMode ? heatmapPreview.paintedWidth : preview.paintedWidth) > 0
                 && (control.heatmapMode ? heatmapPreview.paintedHeight : preview.paintedHeight) > 0
                 && ((control.heatmapMode && (control.record.anomalyModelPolygons || []).length > 0)
                     || (!control.heatmapMode && (control.record.anomalyImagePolygons || []).length > 0))
        polygons: control.heatmapMode ? (control.record.anomalyModelPolygons || [])
                                      : (control.record.anomalyImagePolygons || [])
        coordinateViewport: {
            if (control.heatmapMode)
                return {valid: heatmapPreview.sourceSize.width > 0 && heatmapPreview.sourceSize.height > 0,
                        x: 0, y: 0, width: heatmapPreview.sourceSize.width, height: heatmapPreview.sourceSize.height}
            var viewport = control.viewportRect()
            if (viewport.valid)
                return viewport
            var imageWidth = Number(control.record.imageWidth || 0)
            var imageHeight = Number(control.record.imageHeight || 0)
            return {valid: imageWidth > 0 && imageHeight > 0,
                    x: 0, y: 0, width: imageWidth, height: imageHeight}
        }
        sourceWidth: control.heatmapMode
                     ? heatmapPreview.sourceSize.width
                     : (control.viewportRect().valid ? control.viewportRect().width
                                                      : Number(control.record.imageWidth || 0))
        sourceHeight: control.heatmapMode
                      ? heatmapPreview.sourceSize.height
                      : (control.viewportRect().valid ? control.viewportRect().height
                                                       : Number(control.record.imageHeight || 0))
        paintedWidth: control.heatmapMode ? heatmapPreview.paintedWidth : preview.paintedWidth
        paintedHeight: control.heatmapMode ? heatmapPreview.paintedHeight : preview.paintedHeight
        strokeColor: "#ff5252"
        fillOpacity: 0.12
        lineWidth: 2
    }
}
