import QtQuick
import QtQuick.Controls
import QtQuick.Effects

import dltool.core
import dltool.ui
import dltool.settings
import quickui

/**
 * @brief 标注实例缩略图组件
 * 
 * 显示单个标注的裁剪区域图像，包含边距和标注轮廓。
 * 边距从 Settings 获取，颜色从 LabelClasses 获取
 */
Item {
    id: root
    
    // 公共属性
    property int labelId: -1  // 标注 ID
    property int imageId: -1
    property int method: -1
    property string labelClassName: ""
    property var labelData: null  // 标注数据 {x, y, width, height}
    property color borderColor: QuiColor.Transparent  // 边框颜色（从外部传入）
    property int margin: 10
    property int borderWidth: 2
    property real padding: 10.0
    property real fillOpacity: 0.3
    property real imageBrightness: 0.0
    property real imageContrast: 0.0
    property string providerCacheKey: ""
    
    // 只读属性
    readonly property bool imageLoaded: thumbnail.status === Image.Ready
    readonly property bool imageError: thumbnail.status === Image.Error
    readonly property bool classificationMode: method === DeepLearningMethod.Classification
    readonly property bool hasValidLabelData: classificationMode || isLabelDataValid()
    readonly property bool hasPolygonLabelData: !classificationMode && hasValidLabelData
                                                && labelData.points !== undefined
                                                && labelData.points !== null
                                                && labelData.points.length >= 3
    
    // 极端尺寸处理常量
    readonly property real minVisibleSize: 4.0  // 最小可见尺寸（像素）
    readonly property real minBboxSize: 1.0     // 最小 bbox 尺寸（原始坐标）

    onLabelDataChanged: requestOverlayPaint()
    onBorderColorChanged: requestOverlayPaint()
    onBorderWidthChanged: requestOverlayPaint()
    onMarginChanged: requestOverlayPaint()
    onPaddingChanged: requestOverlayPaint()
    onFillOpacityChanged: requestOverlayPaint()
    onHasValidLabelDataChanged: requestOverlayPaint()
    onHasPolygonLabelDataChanged: requestOverlayPaint()

    function requestOverlayPaint() {
        if (annotationOverlay) {
            annotationOverlay.requestPaint()
        }
    }

    function polygonBounds() {
        if (!labelData || !labelData.points || labelData.points.length < 3) {
            return { valid: false, x: 0, y: 0, width: 0, height: 0 }
        }

        let minX = Number.POSITIVE_INFINITY
        let minY = Number.POSITIVE_INFINITY
        let maxX = Number.NEGATIVE_INFINITY
        let maxY = Number.NEGATIVE_INFINITY
        for (let i = 0; i < labelData.points.length; ++i) {
            let point = labelData.points[i]
            if (!point || typeof point.x !== "number" || typeof point.y !== "number"
                    || isNaN(point.x) || isNaN(point.y)) {
                continue
            }
            minX = Math.min(minX, point.x)
            minY = Math.min(minY, point.y)
            maxX = Math.max(maxX, point.x)
            maxY = Math.max(maxY, point.y)
        }

        if (!isFinite(minX) || !isFinite(minY) || maxX <= minX || maxY <= minY) {
            return { valid: false, x: 0, y: 0, width: 0, height: 0 }
        }
        return { valid: true, x: minX, y: minY, width: maxX - minX, height: maxY - minY }
    }

    function effectiveLabelBounds() {
        let bounds = polygonBounds()
        if (bounds.valid) {
            return bounds
        }

        if (!labelData || typeof labelData.x !== "number" || typeof labelData.y !== "number"
                || typeof labelData.width !== "number" || typeof labelData.height !== "number"
                || isNaN(labelData.x) || isNaN(labelData.y)
                || isNaN(labelData.width) || isNaN(labelData.height)
                || labelData.width <= 0 || labelData.height <= 0) {
            return { valid: false, x: 0, y: 0, width: 0, height: 0 }
        }

        return { valid: true, x: labelData.x, y: labelData.y,
                 width: labelData.width, height: labelData.height }
    }
    
    // 验证 labelData 是否有效
    function isLabelDataValid() {
        return effectiveLabelBounds().valid
    }

    function extendedMarginForBounds(bounds) {
        if (!bounds.valid) {
            return margin
        }
        return Math.max(0, margin) + Math.round(Math.max(0, padding))
    }

    function cropSize() {
        let bounds = effectiveLabelBounds()
        if (!bounds.valid) {
            return { valid: false, width: 0, height: 0 }
        }
        let extendedMargin = extendedMarginForBounds(bounds)
        return {
            valid: true,
            width: Math.max(1, Math.round(bounds.width + 2 * extendedMargin)),
            height: Math.max(1, Math.round(bounds.height + 2 * extendedMargin))
        }
    }
    
    // 图像组件
    Image {
        id: thumbnail
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        
        // URL 包含 padding 查询参数以支持边界扩展
        source: {
            if (root.classificationMode && root.imageId >= 0) {
                return "image://imageinstance/" + root.imageId
                       + "?w=" + Math.round(thumbnail.width)
                       + "&h=" + Math.round(thumbnail.height)
                       + "&project=" + root.providerCacheKey
            }
            return root.labelId >= 0
                    ? "image://labelinstance/" + root.labelId
                      + "?padding=" + root.padding
                      + "&margin=" + root.margin
                      + "&project=" + root.providerCacheKey
                    : ""
        }
        sourceSize.width: root.classificationMode ? width : 0
        sourceSize.height: root.classificationMode ? height : 0
        
        cache: false
        
        // 异步加载
        asynchronous: true
        
        // 监听状态变化（仅在错误时输出日志）
        onStatusChanged: {
            if (status === Image.Error) {
                console.warn("[LabelInstanceThumbnail] Image load error for labelId:", root.labelId);
            }
            requestOverlayPaint()
        }

        onPaintedWidthChanged: requestOverlayPaint()
        onPaintedHeightChanged: requestOverlayPaint()
        onSourceSizeChanged: requestOverlayPaint()
        
        // 加载状态指示
        BusyIndicator {
            anchors.centerIn: parent
            running: thumbnail.status === Image.Loading
            visible: running
        }
        
        // 错误提示 - 当图像加载失败时显示
        // Rectangle {
        //     anchors.centerIn: parent
        //     visible: thumbnail.status === Image.Error
        //     width: Math.min(parent.width * 0.8, 200)
        //     height: Math.min(parent.height * 0.8, 100)
        //     // color: QuiColor.ControlStrokeColorDefault
        //     radius: 4
        //     border.color: "red"
        //     border.width: 1
            
        //     Column {
        //         anchors.centerIn: parent
        //         spacing: 8
                
        //         QuiTextIcon {
        //             anchors.horizontalCenter: parent.horizontalCenter
        //             iconSource: QuiFontIcon.ErrorBadge
        //             iconSize: 24
        //         }
                
        //         QuiText {
        //             anchors.horizontalCenter: parent.horizontalCenter
        //             text: qsTr("Failed to load image")
        //             font.pixelSize: 12
        //         }
                
        //         QuiText {
        //             anchors.horizontalCenter: parent.horizontalCenter
        //             text: qsTr("Label ID: %1").arg(root.labelId)
        //             font.pixelSize: 10
        //         }
        //     }
        // }
    }
    
    // 应用亮度和对比度效果
    MultiEffect {
        source: thumbnail
        anchors.fill: thumbnail
        visible: thumbnail.status === Image.Ready && thumbnail.source.toString().length > 0
        brightness: root.imageBrightness
        contrast: root.imageContrast
    }

    Rectangle {
        id: classIndicator
        visible: root.classificationMode && root.labelClassName.length > 0
        implicitWidth: classText.implicitWidth + 14
        implicitHeight: classText.implicitHeight + 6
        radius: 2
        color: root.borderColor
        border.color: Qt.rgba(1, 1, 1, 0.18)
        border.width: 1
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 4
        z: 2

        Text {
            id: classText
            anchors.fill: parent
            anchors.leftMargin: 7
            anchors.rightMargin: 7
            anchors.topMargin: 2
            anchors.bottomMargin: 2
            text: root.labelClassName
            color: "white"
            font.pixelSize: 12
            font.weight: Font.Medium
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    
    // 标注覆盖层 - 分割显示多边形，检测显示矩形框
    // 仅在以下条件全部满足时显示：
    // 1. labelData 有效 (hasValidLabelData)
    // 2. 图像加载成功 (imageLoaded)
    // 3. 图像没有错误 (!imageError)
    // 4. 图像已绘制 (paintedWidth > 0 && paintedHeight > 0)
    // 5. 图像源尺寸有效 (sourceSize.width > 0 && sourceSize.height > 0)
    Canvas {
        id: annotationOverlay

        anchors.fill: parent
        visible: !root.classificationMode &&
                 hasValidLabelData && imageLoaded && !imageError &&
                 thumbnail.paintedWidth > 0 && thumbnail.paintedHeight > 0 &&
                 cropSize().valid

        onVisibleChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            let ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            if (!visible) {
                return
            }

            let lineWidth = Math.max(1, root.borderWidth)
            ctx.save()
            ctx.lineWidth = lineWidth
            ctx.strokeStyle = String(root.borderColor)
            ctx.fillStyle = String(root.borderColor)

            if (root.hasPolygonLabelData) {
                drawPolygon(ctx)
            } else {
                drawRectangle(ctx, lineWidth)
            }

            ctx.restore()
        }
    }
    
    // 位置计算函数
    function calculateExtendedMargin() {
        if (!hasValidLabelData) {
            return margin
        }
        let bounds = effectiveLabelBounds()
        return extendedMarginForBounds(bounds)
    }
    
    function calculateScale() {
        // 计算图像在 Image 组件中的实际缩放比例
        let size = cropSize()
        if (!hasValidLabelData || !size.valid) {
            return 1.0
        }
        
        // Image 使用 PreserveAspectFit，所以实际显示的图像可能比 Item 小
        // paintedWidth 和 paintedHeight 是实际绘制的图像尺寸
        let scaleX = thumbnail.paintedWidth / size.width
        let scaleY = thumbnail.paintedHeight / size.height
        
        // 由于是 PreserveAspectFit，两个缩放比例应该相同
        return Math.min(scaleX, scaleY)
    }
    
    function calculateImageOffsetX() {
        // 计算图像在 Image 组件中的 X 偏移（居中）
        return (thumbnail.width - thumbnail.paintedWidth) / 2
    }
    
    function calculateImageOffsetY() {
        // 计算图像在 Image 组件中的 Y 偏移（居中）
        return (thumbnail.height - thumbnail.paintedHeight) / 2
    }
    
    function calculateX() {
        if (!hasValidLabelData) return 0
        let extendedMargin = calculateExtendedMargin()
        let scale = calculateScale()
        let offsetX = calculateImageOffsetX()
        
        // 矩形在裁剪图像中的位置是 extended_margin，然后应用缩放和偏移
        let x = offsetX + extendedMargin * scale
        
        // 确保不超出图像左边界
        if (x < offsetX) {
            x = offsetX
        }
        
        // 确保不超出图像右边界（留出至少 minVisibleSize 的空间）
        let maxX = offsetX + thumbnail.paintedWidth - minVisibleSize
        if (x > maxX) {
            x = maxX
        }
        
        return Math.max(offsetX, x)
    }
    
    function calculateY() {
        if (!hasValidLabelData) return 0
        let extendedMargin = calculateExtendedMargin()
        let scale = calculateScale()
        let offsetY = calculateImageOffsetY()
        
        // 矩形在裁剪图像中的位置是 extended_margin，然后应用缩放和偏移
        let y = offsetY + extendedMargin * scale
        
        // 确保不超出图像上边界
        if (y < offsetY) {
            y = offsetY
        }
        
        // 确保不超出图像下边界（留出至少 minVisibleSize 的空间）
        let maxY = offsetY + thumbnail.paintedHeight - minVisibleSize
        if (y > maxY) {
            y = maxY
        }
        
        return Math.max(offsetY, y)
    }
    
    function calculateWidth() {
        if (!hasValidLabelData) return 0
        let scale = calculateScale()
        let bounds = effectiveLabelBounds()
        
        // 矩形宽度保持与原始 bbox 相同，然后应用缩放
        let width = bounds.width * scale
        
        // 确保极小 bbox 仍然可见
        if (width > 0 && width < minVisibleSize) {
            width = minVisibleSize
        }
        
        // 确保不超出图像边界
        // calculateX() 返回的是绝对位置，需要减去 offsetX 得到相对于图像的位置
        let offsetX = calculateImageOffsetX()
        let rectXInImage = calculateX() - offsetX
        let maxWidth = thumbnail.paintedWidth - rectXInImage
        if (maxWidth > 0 && width > maxWidth) {
            width = maxWidth
        }
        
        return Math.max(0, width)
    }
    
    function calculateHeight() {
        if (!hasValidLabelData) return 0
        let scale = calculateScale()
        let bounds = effectiveLabelBounds()
        
        // 矩形高度应该完美贴合 bbox，不包含 padding
        let height = bounds.height * scale
        
        // 确保极小 bbox 仍然可见
        if (height > 0 && height < minVisibleSize) {
            height = minVisibleSize
        }
        
        // 确保不超出图像边界
        // calculateY() 返回的是绝对位置，需要减去 offsetY 得到相对于图像的位置
        let offsetY = calculateImageOffsetY()
        let rectYInImage = calculateY() - offsetY
        let maxHeight = thumbnail.paintedHeight - rectYInImage
        if (maxHeight > 0 && height > maxHeight) {
            height = maxHeight
        }
        
        return Math.max(0, height)
    }

    function drawRectangle(ctx, lineWidth) {
        let x = calculateX()
        let y = calculateY()
        let width = calculateWidth()
        let height = calculateHeight()
        if (width <= 0 || height <= 0) {
            return
        }

        let inset = lineWidth / 2
        ctx.globalAlpha = Math.max(0, Math.min(1, root.fillOpacity))
        ctx.fillRect(x + inset, y + inset,
                     Math.max(0, width - lineWidth),
                     Math.max(0, height - lineWidth))
        ctx.globalAlpha = 1
        ctx.strokeRect(x + inset, y + inset,
                       Math.max(0, width - lineWidth),
                       Math.max(0, height - lineWidth))
    }

    function drawPolygon(ctx) {
        let points = labelData.points
        if (!points || points.length < 3) {
            return
        }

        let first = mapLabelPointToThumbnail(points[0])
        if (!first.valid) {
            return
        }

        ctx.beginPath()
        ctx.moveTo(first.x, first.y)

        for (let i = 1; i < points.length; ++i) {
            let point = mapLabelPointToThumbnail(points[i])
            if (point.valid) {
                ctx.lineTo(point.x, point.y)
            }
        }

        ctx.closePath()
        ctx.globalAlpha = Math.max(0, Math.min(1, root.fillOpacity))
        ctx.fill()
        ctx.globalAlpha = 1
        ctx.stroke()
    }

    function mapLabelPointToThumbnail(point) {
        if (!point || typeof point.x !== "number" || typeof point.y !== "number"
                || isNaN(point.x) || isNaN(point.y)) {
            return { valid: false, x: 0, y: 0 }
        }

        let scale = calculateScale()
        let offsetX = calculateImageOffsetX()
        let offsetY = calculateImageOffsetY()
        let extendedMargin = calculateExtendedMargin()
        let bounds = effectiveLabelBounds()
        return {
            valid: true,
            x: offsetX + (point.x - bounds.x + extendedMargin) * scale,
            y: offsetY + (point.y - bounds.y + extendedMargin) * scale
        }
    }

    function refreshSettings() {
        margin = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.Margin, 10)
        borderWidth = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.BorderWidth, 2)
        padding = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.LabelBorderPadding, 10.0)
        fillOpacity = Math.max(0, Math.min(1, GlobalSettings.valueForField(
                    SettingsAccessor.Data,
                    DataField.FillOpacity,
                    30) / 100.0))
        imageBrightness = GlobalSettings.valueForField(SettingsAccessor.Ui, UiField.Brightness, 0.0)
        imageContrast = GlobalSettings.valueForField(SettingsAccessor.Ui, UiField.Contrast, 0.0)
    }

    Component.onCompleted: refreshSettings()

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            root.refreshSettings()
        }
    }
}
