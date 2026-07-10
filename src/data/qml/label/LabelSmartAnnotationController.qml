import QtQuick

import dltool.core
import dltool.data
import dltool.feature
import dltool.settings

Item {
    id: controller
    visible: false

    property DataManager dataManager: null
    property ImageInstancesModel imageInstances: null
    property LabelClassesModel labelClasses: null
    property SmartAnnotationController smartAnnotation: null
    property var geometry: null
    property var drawingItem: null
    property bool active: false
    property bool segmentationMode: false
    property bool drawingPolygon: false
    property color drawingColor: "red"
    property int refreshInterval: 80
    property int promptMode: LabelCanvasEnums.PointPrompt
    property var points: []
    property var box: ({})
    property bool boxValid: false
    property bool drawingBox: false
    property var boxStart: ({})
    property var hoverPoint: ({})
    property bool hoverPointValid: false
    property var result: ({})
    property bool dirty: false
    property bool useViewportInput: false
    readonly property bool available: smartAnnotation && smartAnnotation.enabled && dataManager
                                      && (dataManager.method === DeepLearningMethod.Detection
                                          || dataManager.method === DeepLearningMethod.Segmentation
                                          || dataManager.method === DeepLearningMethod.AnomalyDetection)

    Timer {
        id: previewTimer
        interval: Math.max(20, controller.refreshInterval)
        repeat: false
        onTriggered: controller.updatePreview()
    }

    onDrawingColorChanged: refreshDrawingPreview()
    onPromptModeChanged: {
        clearHoverPoint()
        drawingBox = false
    }
    onUseViewportInputChanged: {
        dirty = true
        if (active && (promptPoints().length > 0 || boxValid)) {
            previewTimer.restart()
        }
    }

    Connections {
        target: geometry ? geometry.imageItem : null
        function onXChanged() { invalidateViewportInput() }
        function onYChanged() { invalidateViewportInput() }
        function onScaleChanged() { invalidateViewportInput() }
        function onStatusChanged() { invalidateViewportInput() }
    }

    Connections {
        target: geometry && geometry.imageItem ? geometry.imageItem.parent : null
        function onWidthChanged() { invalidateViewportInput() }
        function onHeightChanged() { invalidateViewportInput() }
    }

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() { refreshSettings() }
    }

    function handlePress(pos, button) {
        if (!active || (button !== Qt.LeftButton && button !== Qt.RightButton)) {
            return false
        }
        if (promptMode === LabelCanvasEnums.BoxPrompt && button === Qt.LeftButton) {
            beginBox(pos)
            return true
        }
        appendPoint(pos,
                    button === Qt.LeftButton ? LabelCanvasEnums.ForegroundPrompt
                                             : LabelCanvasEnums.BackgroundPrompt)
        return true
    }

    function handleHover(pos, schedulePreview) {
        if (!active) {
            return
        }
        if (promptMode === LabelCanvasEnums.PointPrompt) {
            setHoverPoint(pos, schedulePreview)
        } else if (hoverPointValid) {
            clearHoverPoint()
        }
    }

    function beginBox(pos) {
        if (!geometry || !geometry.isPointInsideImage(pos)) {
            return
        }
        boxStart = geometry.clampPointToImage(pos)
        drawingBox = true
        clearHoverPoint()
        updateBox(pos)
    }

    function updateBox(pos) {
        if (!drawingBox || !geometry) {
            return
        }
        let end = geometry.clampPointToImage(pos)
        box = geometry.rectFromPoints(boxStart, end)
        boxValid = box.width > 1 && box.height > 1
        dirty = true
    }

    function finishBox(pos) {
        updateBox(pos)
        drawingBox = false
        if (boxValid) {
            updatePreview()
        } else if (points.length > 0) {
            updatePreview()
        } else {
            clearPreview()
        }
    }

    function handleExited() {
        if (active && hoverPointValid) {
            clearHoverPoint()
            dirty = true
            previewTimer.restart()
        }
    }

    function clearHoverPoint() {
        hoverPoint = ({})
        hoverPointValid = false
    }

    function setHoverPoint(pos, schedulePreview) {
        if (!active || !geometry) {
            return
        }
        if (!geometry.isPointInsideImage(pos)) {
            if (hoverPointValid) {
                clearHoverPoint()
                dirty = true
                if (schedulePreview) {
                    previewTimer.restart()
                }
            }
            return
        }

        let imagePos = geometry.clampPointToImage(pos)
        if (hoverPointValid && geometry.distance(imagePos, hoverPoint) < 0.5) {
            return
        }

        hoverPoint = {
            x: imagePos.x,
            y: imagePos.y,
            label: LabelCanvasEnums.ForegroundPrompt
        }
        hoverPointValid = true
        dirty = true
        if (schedulePreview) {
            previewTimer.restart()
        }
    }

    function appendPoint(pos, label) {
        if (!active || !geometry || !geometry.isPointInsideImage(pos)) {
            return
        }

        let imagePos = geometry.clampPointToImage(pos)
        let updatedPoints = geometry.cloneSmartPoints(points)
        updatedPoints.push({x: imagePos.x, y: imagePos.y, label: label})
        points = updatedPoints
        clearHoverPoint()
        dirty = true
        updatePreview()
    }

    function promptPoints() {
        let promptList = geometry ? geometry.cloneSmartPoints(points) : []
        if (hoverPointValid) {
            promptList.push({
                                x: hoverPoint.x,
                                y: hoverPoint.y,
                                label: LabelCanvasEnums.ForegroundPrompt
                            })
        }
        return promptList
    }

    function inferenceOptions() {
        let viewport = useViewportInput ? visibleViewportInput() : ({})
        return {
            use_viewport_input: useViewportInput && viewport.width > 0 && viewport.height > 0,
            viewport: viewport,
            prompt_box: boxValid ? box : ({})
        }
    }

    function visibleViewportInput() {
        let imageItem = geometry ? geometry.imageItem : null
        if (!imageItem || !imageItem.parent || imageItem.status !== Image.Ready || imageItem.scale <= 0) {
            return ({})
        }

        let scale = imageItem.scale
        let sourceWidth = imageItem.sourceSize.width
        let sourceHeight = imageItem.sourceSize.height
        let left = Math.max(0, Math.floor(-imageItem.x / scale))
        let top = Math.max(0, Math.floor(-imageItem.y / scale))
        let right = Math.min(sourceWidth, Math.ceil((imageItem.parent.width - imageItem.x) / scale))
        let bottom = Math.min(sourceHeight, Math.ceil((imageItem.parent.height - imageItem.y) / scale))
        let width = right - left
        let height = bottom - top
        if (width <= 0 || height <= 0) {
            return ({})
        }

        return {
            x: left,
            y: top,
            width: width,
            height: height,
            input_width: Math.max(1, Math.round(width * scale)),
            input_height: Math.max(1, Math.round(height * scale))
        }
    }

    function invalidateViewportInput() {
        if (!useViewportInput || !active) {
            return
        }

        dirty = true
        if (promptPoints().length > 0 || boxValid) {
            previewTimer.restart()
        }
    }

    function updatePreview() {
        previewTimer.stop()
        let prompts = promptPoints()
        if (!canRun(prompts)) {
            clearPreview()
            return
        }

        let inferResult = smartAnnotation.infer(imageInstances.currentImagePath, prompts, inferenceOptions())
        if (!isValidResult(inferResult)) {
            clearPreview()
            return
        }

        applyPreview(inferResult)
    }

    function canRun(prompts) {
        return active && smartAnnotation && imageInstances && (prompts.length > 0 || boxValid)
    }

    function isValidResult(inferResult) {
        return inferResult && inferResult.success === true
    }

    function clearPreview() {
        result = ({})
        dirty = false
        if (drawingItem) {
            drawingItem.clearItem()
        }
    }

    function applyPreview(inferResult) {
        result = inferResult
        dirty = false
        refreshDrawingPreview()
    }

    function refreshDrawingPreview() {
        if (!active || !drawingItem || !isValidResult(result)) {
            return
        }

        if (segmentationMode && result.points && result.points.length >= 3) {
            drawingItem.updateItem({
                                       label_id: -1,
                                       points: result.points,
                                       color: drawingColor
                                   })
        } else {
            drawingItem.updateItem({
                                       label_id: -1,
                                       x: result.x,
                                       y: result.y,
                                       width: result.width,
                                       height: result.height,
                                       color: drawingColor
                                   })
        }
    }

    function confirm(mouseInside, mouseX, mouseY) {
        if (!canAdd()) {
            return null
        }

        ensurePromptFromMouse(mouseInside, mouseX, mouseY)
        if (promptPoints().length === 0) {
            return null
        }

        if (!ensureResult()) {
            return null
        }

        return buildLabelData(result)
    }

    function canAdd() {
        return dataManager && imageInstances && labelClasses && labelClasses.currentLabelClassId !== -1
    }

    function ensurePromptFromMouse(mouseInside, mouseX, mouseY) {
        if (promptMode === LabelCanvasEnums.PointPrompt && !hoverPointValid && points.length === 0
                && !boxValid && mouseInside && geometry) {
            setHoverPoint(geometry.getPosOnImagePoint(mouseX, mouseY), false)
        }
    }

    function ensureResult() {
        if (dirty || !isValidResult(result)) {
            updatePreview()
        }
        return isValidResult(result)
    }

    function buildLabelData(inferResult) {
        if (!geometry) {
            return null
        }
        if (segmentationMode && inferResult.points && inferResult.points.length >= 3) {
            return {
                points: geometry.clonePoints(inferResult.points),
                x: inferResult.x,
                y: inferResult.y,
                width: inferResult.width,
                height: inferResult.height
            }
        }
        if (inferResult.width > 1 && inferResult.height > 1) {
            return {
                x: inferResult.x,
                y: inferResult.y,
                width: inferResult.width,
                height: inferResult.height
            }
        }
        return null
    }

    function clear() {
        previewTimer.stop()
        points = []
        box = ({})
        boxValid = false
        drawingBox = false
        boxStart = ({})
        hoverPoint = ({})
        hoverPointValid = false
        result = ({})
        dirty = false
        if (drawingItem && !drawingPolygon) {
            drawingItem.clearItem()
        }
    }

    function refreshSettings() {
        useViewportInput = GlobalSettings.valueForField(
                    SettingsAccessor.SmartAnnotation,
                    SmartAnnotationField.UseViewportInput,
                    false)
    }

    Component.onCompleted: refreshSettings()
}
