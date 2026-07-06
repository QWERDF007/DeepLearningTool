import QtQuick

import dltool.core
import dltool.data
import dltool.feature

Item {
    id: smartLayer
    visible: false

    property var canvas
    property var drawingItem
    property bool segmentationMode: false
    property bool drawingPolygon: false
    property int refreshInterval: 80
    property var addLabelHandler: function(data) { return false }

    readonly property bool active: canvas && canvas.toolMode === LabelCanvasEnums.SmartTool
                                   && smartAnnotationController.available
    readonly property bool available: smartAnnotationController.available
    property alias result: smartAnnotationController.result
    property alias points: smartAnnotationController.points
    property alias hoverPoint: smartAnnotationController.hoverPoint
    property alias hoverPointValid: smartAnnotationController.hoverPointValid

    readonly property ImageInstancesModel imageInstances: canvas ? canvas.imageInstances : null
    readonly property LabelClassesModel labelClasses: canvas ? canvas.labelClasses : null

    LabelSmartAnnotationController {
        id: smartAnnotationController
        dataManager: smartLayer.canvas ? smartLayer.canvas.dataManager : null
        imageInstances: smartLayer.imageInstances
        labelClasses: smartLayer.labelClasses
        smartAnnotation: smartLayer.canvas && smartLayer.canvas.featureManager
                         ? smartLayer.canvas.featureManager.smartAnnotation
                         : null
        geometry: smartLayer.canvas ? smartLayer.canvas.geometry : null
        drawingItem: smartLayer.drawingItem
        active: smartLayer.active
        segmentationMode: smartLayer.segmentationMode
        drawingPolygon: smartLayer.drawingPolygon
        drawingColor: smartLayer.canvas ? smartLayer.canvas.drawingColor : "red"
        refreshInterval: smartLayer.refreshInterval
    }

    Connections {
        target: smartAnnotationController.smartAnnotation
        function onModelLoadFinished(success) {
            if (success && smartLayer.active) {
                smartAnnotationController.updatePreview()
            }
        }
    }

    function handleToolModeChanged() {
        if (!canvas) {
            return
        }

        if (!active) {
            smartAnnotationController.clear()
            return
        }

        if (canvas.canvasMouseArea.containsMouse) {
            smartAnnotationController.setHoverPoint(
                        canvas.geometry.getPosOnImagePoint(canvas.canvasMouseArea.mouseX,
                                                           canvas.canvasMouseArea.mouseY),
                        true)
        }
    }

    function handleCurrentImageChanged() {
        smartAnnotationController.clear()
    }

    function handleDrawingColorChanged() {
        smartAnnotationController.refreshDrawingPreview()
    }

    function handleMouseExited() {
        smartAnnotationController.handleExited()
    }

    function handleKeyPressed(event) {
        if (!active) {
            return false
        }

        if (event.key === Qt.Key_Escape) {
            if (smartAnnotationController.points.length > 0) {
                smartAnnotationController.clear()
            } else if (canvas) {
                canvas.setToolMode(LabelCanvasEnums.SelectTool)
            }
            event.accepted = true
            return true
        }

        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            let data = smartAnnotationController.confirm(canvas.canvasMouseArea.containsMouse,
                                                         canvas.canvasMouseArea.mouseX,
                                                         canvas.canvasMouseArea.mouseY)
            if (data && addLabelHandler(data)) {
                smartAnnotationController.clear()
            }
            event.accepted = true
            return true
        }

        return false
    }

    function handleMousePressed(pos, button) {
        return smartAnnotationController.handlePress(pos, button)
    }

    function shouldConsumeIdleRelease(button) {
        return active
                && canvas
                && canvas.interactionState === LabelCanvasEnums.Idle
                && (button === Qt.LeftButton || button === Qt.RightButton)
    }

    function handleMousePositionChanged(pos) {
        if (active && canvas && canvas.interactionState === LabelCanvasEnums.Idle) {
            smartAnnotationController.handleHover(pos, true)
            return true
        }
        return false
    }

    function clear() {
        smartAnnotationController.clear()
    }
}
