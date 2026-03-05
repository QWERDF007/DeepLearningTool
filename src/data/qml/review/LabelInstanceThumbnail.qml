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
    readonly property bool hasValidLabelData: isLabelDataValid()
    
    // 配置属性（从 GlobalSettings 获取）
    readonly property int margin: GlobalSettings.data.thumbnailMargin
    readonly property int borderWidth: GlobalSettings.data.labelBorderWidth
    readonly property real padding: GlobalSettings.data.labelThumbnailBorderPadding
    
    // 极端尺寸处理常量
    readonly property real minVisibleSize: 4.0  // 最小可见尺寸（像素）
    readonly property real minBboxSize: 1.0     // 最小 bbox 尺寸（原始坐标）
    
    // 验证 labelData 是否有效
    function isLabelDataValid() {
        if (!labelData) {
            return false
        }
        
        // 检查必需的属性是否存在且为有效数值
        if (typeof labelData.x !== 'number' || isNaN(labelData.x)) {
            console.warn("[LabelInstanceThumbnail] Invalid labelData.x:", labelData.x)
            return false
        }
        if (typeof labelData.y !== 'number' || isNaN(labelData.y)) {
            console.warn("[LabelInstanceThumbnail] Invalid labelData.y:", labelData.y)
            return false
        }
        if (typeof labelData.width !== 'number' || isNaN(labelData.width) || labelData.width <= 0) {
            console.warn("[LabelInstanceThumbnail] Invalid labelData.width:", labelData.width)
            return false
        }
        if (typeof labelData.height !== 'number' || isNaN(labelData.height) || labelData.height <= 0) {
            console.warn("[LabelInstanceThumbnail] Invalid labelData.height:", labelData.height)
            return false
        }
        
        return true
    }
    
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
        
        // 错误提示 - 当图像加载失败时显示
        // Rectangle {
        //     anchors.centerIn: parent
        //     visible: thumbnail.status === Image.Error
        //     width: Math.min(parent.width * 0.8, 200)
        //     height: Math.min(parent.height * 0.8, 100)
        //     // color: DltColor.ControlStrokeColorDefault
        //     radius: 4
        //     border.color: "red"
        //     border.width: 1
            
        //     Column {
        //         anchors.centerIn: parent
        //         spacing: 8
                
        //         DltTextIcon {
        //             anchors.horizontalCenter: parent.horizontalCenter
        //             iconSource: DltFontIcon.ErrorBadge
        //             iconSize: 24
        //         }
                
        //         DltText {
        //             anchors.horizontalCenter: parent.horizontalCenter
        //             text: qsTr("Failed to load image")
        //             font.pixelSize: 12
        //         }
                
        //         DltText {
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
        brightness: GlobalSettings.ui.imageBrightness
        contrast: GlobalSettings.ui.imageContrast
    }
    
    // 矩形覆盖层 - 显示标注边框
    // 仅在以下条件全部满足时显示：
    // 1. labelData 有效 (hasValidLabelData)
    // 2. 图像加载成功 (imageLoaded)
    // 3. 图像没有错误 (!imageError)
    // 4. 图像已绘制 (paintedWidth > 0 && paintedHeight > 0)
    // 5. 图像源尺寸有效 (sourceSize.width > 0 && sourceSize.height > 0)
    Rectangle {
        id: boundingBox
        visible: hasValidLabelData && imageLoaded && !imageError && 
                 thumbnail.paintedWidth > 0 && thumbnail.paintedHeight > 0 &&
                 thumbnail.sourceSize.width > 0 && thumbnail.sourceSize.height > 0
        
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
        if (!hasValidLabelData) {
            return margin
        }
        let maxDimension = Math.max(labelData.width, labelData.height)
        // 注意：C++ 端使用 static_cast<int>，所以这里也要使用 Math.floor 来保持一致
        return margin + Math.floor(padding * maxDimension)
    }
    
    function calculateScale() {
        // 计算图像在 Image 组件中的实际缩放比例
        if (!hasValidLabelData || thumbnail.sourceSize.width === 0 || thumbnail.sourceSize.height === 0) {
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
        
        // 矩形宽度保持与原始 bbox 相同，然后应用缩放
        let width = labelData.width * scale
        
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
        
        // 矩形高度应该完美贴合 bbox，不包含 padding
        let height = labelData.height * scale
        
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
}
