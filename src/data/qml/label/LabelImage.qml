import QtQuick
import QtQuick.Controls
import QtQuick.Effects

import dltool.settings
import dltool.ui

Item {
    id: labelImage
    clip: true

    default property alias children: image.data

    property bool isInit: false
    property bool needFitInView: false
    property bool isDragging: mouseArea.drag.active
    property bool scalable: true
    property real imageBrightness: 0.0
    property real imageContrast: 0.0

    property alias image: image
    
    property string currentImagePath: ""

    property real imageSourceScale: {
        if (image.source !== Qt.url("") && image.status === image.Ready) {
            return Math.min(labelImage.height / image.sourceSize.height, labelImage.width / image.sourceSize.width)
        }
        return 1.0
    }
    readonly property bool imageReady: currentImagePath.length > 0 && image.status === Image.Ready

    property real stepSize: {
        // 基于图像原始大小和当前显示大小计算基础步长
        let baseStep = Math.min(image.sourceSize.width / labelImage.width,
                                image.sourceSize.height / labelImage.height) * 0.1
        // 当缩放比例较小或图像未充满视图时使用较小步长
        // if (image.scale < 2 || image.paintedWidth * image.scale < labelImage.width ||
        //     image.paintedHeight * image.scale < labelImage.height) {
        //     return Math.min(0.1, baseStep)
        // }
        
        // 使用指数函数根据原始图像大小和当前缩放比例计算平滑步长
        // 较大图像会有较大的步长，较小图像会有较小的步长
        return Math.min(Math.max(baseStep * Math.pow(1.5, image.scale - 2), 0.1), 8.0)
    }

    property var scaledImagePos: mapFromItem(image, 0, 0)

    property real from: 0.25
    property real to: 32

    onCurrentImagePathChanged: resetViewState()
    
        
    Image {
        id: image
        visible: false  // 隐藏原始图像，使用 MultiEffect 显示
        smooth: false
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        source: currentImagePath ? Utils.toFileUrl(currentImagePath) : ""
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
                if (isInit) {
                    labelImage.fitInView()
                } else {
                    needFitInView = true
                }
            }
        }
    }
    
    // 应用亮度和对比度效果
    MultiEffect {
        source: image
        anchors.fill: image
        x: image.x
        y: image.y
        scale: image.scale
        visible: labelImage.imageReady
        transformOrigin: Item.TopLeft
        brightness: labelImage.imageBrightness
        contrast: labelImage.imageContrast
    }
    
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        enabled: labelImage.visible && labelImage.imageReady
        drag.target: null
        drag.axis: Drag.XAndYAxis
        acceptedButtons: Qt.AllButtons
        onPressed: function(event) {
            if (event.modifiers & Qt.ControlModifier) {
                drag.target = image
            } else if (event.button === Qt.MiddleButton) {
                drag.target = image
            } else {
                drag.target = null
            }
        }
        onWheel: function(event) {
            if (labelImage.scalable) {
                labelImage.scaleImageByWheel(event)
                labelImage.updateImagePos()
            }
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

    function resetViewState() {
        if (typeof image === "undefined" || image === null) {
            return
        }
        needFitInView = false
        image.x = 0
        image.y = 0
        image.scale = 1.0
        scaledImagePos = Qt.point(0, 0)
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

    // 执行顺序如下, 在可见情况下使图像适应窗口
    // onWidthChanged -> onHeightChanged -> onCompleted -> onVisibleChanged ->
    // onVisibleChanged -> onWidthChanged -> onHeightChanged
    onVisibleChanged: {
        if (visible && !isInit) {
            isInit = true
        }
    }
    onWidthChanged: {
    }
    onHeightChanged: {
        if (visible && isInit && needFitInView) {
            labelImage.fitInView()
            needFitInView = false
        }
    }
    Component.onCompleted: {
        refreshSettings()
    }

    function refreshSettings() {
        imageBrightness = GlobalSettings.valueForField(SettingsAccessor.Ui, UiField.Brightness, 0.0)
        imageContrast = GlobalSettings.valueForField(SettingsAccessor.Ui, UiField.Contrast, 0.0)
    }

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            labelImage.refreshSettings()
        }
    }
}
