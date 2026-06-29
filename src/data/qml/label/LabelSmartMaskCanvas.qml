import QtQuick

Canvas {
    id: maskCanvas

    property bool active: false
    property var imageItem: null
    property var result: ({})
    property color fillColor: "red"
    property real maskAlpha: 0.35

    antialiasing: false
    visible: active
             && result
             && result.success === true
             && result.mask_runs
             && result.mask_runs.length > 0
             && maskAlpha > 0

    onPaint: {
        let ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        if (!visible || !imageItem) {
            return
        }

        let scale = imageItem.scale
        let alpha = Math.max(0, Math.min(1, maskAlpha))
        ctx.save()
        ctx.fillStyle = Qt.rgba(fillColor.r, fillColor.g, fillColor.b, alpha)
        for (let run of result.mask_runs) {
            let runHeight = run.height === undefined ? 1 : run.height
            ctx.fillRect(imageItem.x + run.x * scale,
                         imageItem.y + run.y * scale,
                         Math.max(1, run.width * scale),
                         Math.max(1, runHeight * scale))
        }
        ctx.restore()
    }

    onVisibleChanged: requestPaint()
    onResultChanged: requestPaint()
    onFillColorChanged: requestPaint()
    onMaskAlphaChanged: requestPaint()
}
