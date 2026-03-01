import QtQuick
import QtQuick.Controls
import QtQuick.Effects

import dltool.ui
import dltool.settings

/**
 * @brief 标注实例缩略图组件
 * 
 * 显示单个标注的裁剪区域图像，包含边距和矩形框。
 * 边距从 Settings 获取，颜色从 LabelClasses 获取
 */
Item {
    id: root
    
    // 公共属性
    property int labelId: -1  // 标注 ID
    property var labelData: null  // 标注数据 {x, y, width, height}
    property color borderColor: DltColor.Transparent  // 边框颜色（从外部传入）
    
    // 只读属性
    readonly property bool imageLoaded: thumbnail.status === Image.Ready
    readonly property bool imageError: thumbnail.status === Image.Error
    
    // 配置属性（从 GlobalSettings 获取）
    readonly property int margin: GlobalSettings.data.thumbnailMargin
    readonly property int borderWidth: GlobalSettings.data.labelBorderWidth
    readonly property real padding: GlobalSettings.data.labelThumbnailBorderPadding
    
    // 图像组件
    Image {
        id: thumbnail
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        
        // URL 包含 padding 查询参数以支持边界扩展
        source: root.labelId >= 0 
            ? "image://labelinstance/" + root.labelId 
              + "?padding=" + GlobalSettings.data.labelThumbnailBorderPadding
            : ""
        
        // 缓存控制 - 启用缓存以提升性能
        cache: true
        
        // 异步加载
        asynchronous: true
        
        // 监听状态变化（仅在错误时输出日志）
        onStatusChanged: {
            if (status === Image.Error) {
                console.warn("[LabelInstanceThumbnail] Image load error for labelId:", root.labelId);
            }
        }
        
        // 加载状态指示
        BusyIndicator {
            anchors.centerIn: parent
            running: thumbnail.status === Image.Loading
            visible: running
        }
        
        // 错误提示
        Text {
            anchors.centerIn: parent
            visible: thumbnail.status === Image.Error
            text: "Failed to load image"
            color: "red"
        }
    }
    
    // 应用亮度和对比度效果
    MultiEffect {
        source: thumbnail
        anchors.fill: thumbnail
        brightness: GlobalSettings.ui.imageBrightness
        contrast: GlobalSettings.ui.imageContrast
    }
    
    // 矩形覆盖层 - 显示标注边框
    Rectangle {
        id: boundingBox
        visible: labelData && imageLoaded && !imageError && thumbnail.paintedWidth > 0
        
        // 使用计算函数绑定位置和尺寸
        x: calculateX()
        y: calculateY()
        width: calculateWidth()
        height: calculateHeight()
        
        // 透明背景，只显示边框
        color: "transparent"
        border.color: root.borderColor
        border.width: root.borderWidth
    }
    
    // 位置计算函数
    function calculateExtendedMargin() {
        if (!labelData || !labelData.width || !labelData.height) {
            return margin
        }
        let maxDimension = Math.max(labelData.width, labelData.height)
        return margin + padding * maxDimension
    }
    
    function calculateScale() {
        // 计算图像在 Image 组件中的实际缩放比例
        if (!labelData || thumbnail.sourceSize.width === 0 || thumbnail.sourceSize.height === 0) {
            return 1.0
        }
        
        // Image 使用 PreserveAspectFit，所以实际显示的图像可能比 Item 小
        // paintedWidth 和 paintedHeight 是实际绘制的图像尺寸
        let scaleX = thumbnail.paintedWidth / thumbnail.sourceSize.width
        let scaleY = thumbnail.paintedHeight / thumbnail.sourceSize.height
        
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
        if (!labelData) return 0
        let extendedMargin = calculateExtendedMargin()
        let scale = calculateScale()
        let offsetX = calculateImageOffsetX()
        
        // 矩形在裁剪图像中的位置是扩展边距，然后应用缩放和偏移
        return offsetX + extendedMargin * scale
    }
    
    function calculateY() {
        if (!labelData) return 0
        let extendedMargin = calculateExtendedMargin()
        let scale = calculateScale()
        let offsetY = calculateImageOffsetY()
        
        return offsetY + extendedMargin * scale
    }
    
    function calculateWidth() {
        if (!labelData || !labelData.width) return 0
        let scale = calculateScale()
        
        // 矩形宽度保持与原始 bbox 相同，然后应用缩放
        return labelData.width * scale
    }
    
    function calculateHeight() {
        if (!labelData || !labelData.height) return 0
        let scale = calculateScale()
        
        // 矩形高度保持与原始 bbox 相同，然后应用缩放
        return labelData.height * scale
    }
}
