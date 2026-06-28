import QtQuick

import dltool.core
import dltool.data
import dltool.feature

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
    property var points: []
    property var hoverPoint: ({})
    property bool hoverPointValid: false
    property var result: ({})
    property bool dirty: false
    readonly property bool available: smartAnnotation && smartAnnotation.enabled && dataManager
                                      && (dataManager.method === DeepLearningMethod.Detection
                                          || dataManager.method === DeepLearningMethod.Segmentation)

    Timer {
        id: previewTimer
        interval: Math.max(20, controller.refreshInterval)
        repeat: false
        onTriggered: controller.updatePreview()
    }

    onDrawingColorChanged: refreshDrawingPreview()

    function handlePress(pos, button) {
        if (!active || (button !== Qt.LeftButton && button !== Qt.RightButton)) {
            return false
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
        setHoverPoint(pos, schedulePreview)
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

    function updatePreview() {
        previewTimer.stop()
        let prompts = promptPoints()
        if (!canRun(prompts)) {
            clearPreview()
            return
        }

        let inferResult = smartAnnotation.infer(imageInstances.currentImagePath, prompts)
        if (!isValidResult(inferResult)) {
            clearPreview()
            return
        }

        applyPreview(inferResult)
    }

    function canRun(prompts) {
        return active && smartAnnotation && imageInstances && prompts.length > 0
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
        if (!hoverPointValid && points.length === 0 && mouseInside && geometry) {
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
        hoverPoint = ({})
        hoverPointValid = false
        result = ({})
        dirty = false
        if (drawingItem && !drawingPolygon) {
            drawingItem.clearItem()
        }
    }
}
