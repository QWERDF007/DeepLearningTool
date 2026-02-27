import QtQuick
import QtQuick.Controls

import dltool.ui

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
    
    // 监听 labelId 变化（移除日志以提升性能）
    // onLabelIdChanged: {
    //     console.log("[LabelInstanceThumbnail] labelId changed to:", labelId);
    //     console.log("[LabelInstanceThumbnail] Will request image:", "image://labelinstance/" + labelId);
    // }
    
    // 图像组件
    Image {
        id: thumbnail
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        
        // 简化的 URL，只需要 label_id（使用小写 labelinstance）
        source: root.labelId >= 0 ? "image://labelinstance/" + root.labelId : ""
        
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
}
