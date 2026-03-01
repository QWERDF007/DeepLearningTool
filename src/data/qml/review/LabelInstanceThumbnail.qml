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
    
    // 只读属性
    readonly property bool imageLoaded: thumbnail.status === Image.Ready
    readonly property bool imageError: thumbnail.status === Image.Error
    
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
}
