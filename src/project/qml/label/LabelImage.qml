import QtQuick
import QtQuick.Controls

import dltool.project

Item {
    id: labelImage
    clip: true
    property bool _init: false

    property alias image: image
    property bool isFitInView: true
    
    property Project project: ProjectManager.currentProject
    property string curImagePath: project ? project.imageInstances.curImagePath : ""

    property real imageSourceScale: {
        if (image.source !== Qt.url("") && image.status === image.Ready) {
            return Math.min(labelImage.height / image.sourceSize.height, labelImage.width / image.sourceSize.width)
        }
        return 1.0
    }

    property real stepSize: {
        if (image.scale < 2 || image.paintedWidth * image.scale < labelImage.width || image.paintedHeight * image.scale < labelImage.height) {
            return 0.1
        } else if (image.scale < 10) {
            return 1
        } else {
            return 2
        }
    }

    property var scaledImagePos: mapFromItem(image, 0, 0)

    property real from: 0.1
    property real to: 32
    
        
    Image {
        id: image
        smooth: false
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        source: curImagePath ? "file:///" + curImagePath : ""
        transformOrigin: Item.TopLeft

        onXChanged: {
            labelImage.updateImagePos()
        }
        onYChanged: {
            labelImage.updateImagePos()
        }
        onScaleChanged: {

        }
        onStatusChanged: {
            if (image.status === Image.Ready) {
                if (isFitInView) {
                    labelImage.fitInView()
                } else {
                    labelImage.scaleInCenter(1.0)
                }
            }
        }
    }
    
    MouseArea {
        anchors.fill: parent
        enabled: labelImage.visible && image.status === Image.Ready
        drag.target: image
        drag.axis: Drag.XAndYAxis
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        onWheel: function(event) {
            labelImage.scaleImageByWheel(event)
            labelImage.updateImagePos()
        }
    }

    onWidthChanged: {
        if (isFitInView) {
            labelImage.fitInView()
        } else if (!labelImage._init && labelImage.width && labelImage.height) {
            labelImage.scaleInCenter(1.0)
            labelImage._init = true
        }
    }
    onHeightChanged: {
        if (isFitInView) {
            labelImage.fitInView()
        } else if (!labelImage._init && labelImage.width && labelImage.height) {
            labelImage.scaleInCenter(1.0)
            labelImage._init = true
        }
    }

    /**
     * @brief 在中心缩放图像
     * @param scale
     */
    function scaleInCenter(scale) {
        if (labelImage.width === 0 || labelImage.height === 0 || image.sourceSize.height === 0 || image.sourceSize.width === 0)
            return
        // 缩放后的原点
        var scaleOrigin = mapToItem(image, 0, 0)
        image.scale = Math.min(Math.max(from, scale), to)
        var dx = (labelImage.width - image.sourceSize.width * image.scale) / 2
        var dy = (labelImage.height - image.sourceSize.height * image.scale) / 2
        var pos = mapFromItem(image, scaleOrigin)
        // 按照差值移动一下图，使得图看起来在(0,0)处缩放
        image.x -= pos.x
        image.y -= pos.y
        // 移动到窗口中央
        image.x -= scaledImagePos.x - dx
        image.y -= scaledImagePos.y - dy
        // 不能将上述四条语句合并，因为 image.x/y 改变时会调用信号槽改变 scaledImagePos
    }

    /**
     * @brief 鼠标滚轮缩放图片, 更新图片位置
     * @param wheel
     */
    function scaleImageByWheel(wheel) {
        if (labelImage.width === 0 || labelImage.height === 0 || image.sourceSize.height === 0 || image.sourceSize.width === 0)
            return
        // 鼠标相对于缩放前图像的位置
        var scaleOrigin = mapToItem(image, wheel.x, wheel.y)
        // 缩放
        var step = wheel.angleDelta.y / 120 * labelImage.stepSize
        // _image.scale = Math.min(Math.max(from * scalableImage.imageSourceScale, _image.scale + step), to * scalableImage.imageSourceScale)
        image.scale = Math.min(Math.max(from, image.scale + step), to)
        // 鼠标位置相对于缩放后图像的位置
        var pos = mapFromItem(image, scaleOrigin)
        //按照差值移动一下图，使得图看起来在鼠标位置缩放
        image.x -= pos.x - wheel.x
        image.y -= pos.y - wheel.y
    }
    
    /**
     * @brief 更新缩放后的图像起始点
     */
    function updateImagePos() {
        scaledImagePos = mapFromItem(image, 0, 0)
    }

    /**
     * @brief 图像适应窗口
     */
    function fitInView() {
        if (labelImage.width === 0 || labelImage.height === 0 || image.sourceSize.height === 0 || image.sourceSize.width === 0)
            return
        labelImage.imageSourceScale = Math.min(labelImage.height / image.sourceSize.height, labelImage.width / image.sourceSize.width)
        // 缩放后的原点
        var scaleOrigin = mapToItem(image, 0, 0)
        // 不设置, 图像无法适应窗口大小
        image.scale = labelImage.imageSourceScale
        var dx = (labelImage.width - image.sourceSize.width * image.scale) / 2
        var dy = (labelImage.height - image.sourceSize.height * image.scale) / 2
        var pos = mapFromItem(image, scaleOrigin)
        // 按照差值移动一下图，使得图看起来在(0,0)处缩放
        image.x -= pos.x
        image.y -= pos.y
        // 移动到窗口中央
        image.x -= scaledImagePos.x - dx
        image.y -= scaledImagePos.y - dy
    }

}
