import QtQuick

import dltool.core
import dltool.data

Item {
    id: inputRouter
    visible: false

    property var canvas
    property var visuals
    property var smartLayer
    property var polygonLayer
    property var shapeEditor
    property bool rectangleToolAvailable: true
    property bool polygonRegionMode: false
    property bool imageLevelLabelMode: false
    property var afterLabelAdded: function(imageId, labelClassId, data) {}

    readonly property ImageInstancesModel imageInstances: canvas ? canvas.imageInstances : null
    readonly property LabelClassesModel labelClasses: canvas ? canvas.labelClasses : null
    readonly property bool smartAnnotationMode: smartLayer ? smartLayer.active : false
    readonly property bool selectToolMode: canvas && canvas.toolMode === LabelCanvasEnums.SelectTool
    readonly property bool rectangleToolMode: canvas && canvas.toolMode === LabelCanvasEnums.RectangleTool
    readonly property bool polygonToolMode: canvas && canvas.toolMode === LabelCanvasEnums.PolygonTool
                                         && polygonRegionMode
    readonly property bool smartToolAvailable: smartLayer ? smartLayer.available : false

    Component.onCompleted: installHandlers()
    onCanvasChanged: installHandlers()

    onSmartToolAvailableChanged: {
        if (!canvas) {
            return
        }
        if (!smartToolAvailable && canvas.toolMode === LabelCanvasEnums.SmartTool) {
            canvas.setToolMode(LabelCanvasEnums.SelectTool)
        }
        if (!smartToolAvailable && smartLayer) {
            smartLayer.clear()
        }
    }

    onPolygonRegionModeChanged: {
        if (canvas && !polygonRegionMode && canvas.toolMode === LabelCanvasEnums.PolygonTool) {
            canvas.setToolMode(LabelCanvasEnums.SelectTool)
        }
    }

    onRectangleToolAvailableChanged: {
        if (canvas && !rectangleToolAvailable && canvas.toolMode === LabelCanvasEnums.RectangleTool) {
            canvas.setToolMode(LabelCanvasEnums.SelectTool)
        }
    }

    function installHandlers() {
        if (!canvas) {
            return
        }

        canvas.toolAvailableHandler = function(mode) { return inputRouter.isToolAvailable(mode) }
        canvas.toolModeChangedHandler = function() { inputRouter.handleToolModeChanged() }
        canvas.currentImageChangedHandler = function() { inputRouter.handleCurrentImageChanged() }
        canvas.drawingColorChangedHandler = function() { inputRouter.handleDrawingColorChanged() }
        canvas.mouseExitedHandler = function() { if (smartLayer) smartLayer.handleMouseExited() }
        canvas.toolKeyPressedHandler = function(event) { return inputRouter.handleToolKeyPressed(event) }
        canvas.toolMousePressedHandler = function(event) { return inputRouter.handleToolMousePressed(event) }
        canvas.toolMouseReleasedHandler = function(event) { return inputRouter.handleToolMouseReleased(event) }
        canvas.toolMousePositionChangedHandler = function(event) {
            return inputRouter.handleToolMousePositionChanged(event)
        }
        canvas.toolMouseDoubleClickedHandler = function(event) {
            return inputRouter.handleToolMouseDoubleClicked(event)
        }
    }

    function isToolAvailable(mode) {
        if (mode === LabelCanvasEnums.SelectTool) {
            return true
        }
        if (mode === LabelCanvasEnums.RectangleTool) {
            return rectangleToolAvailable
        }
        if (mode === LabelCanvasEnums.PolygonTool) {
            return polygonRegionMode
        }
        if (mode === LabelCanvasEnums.SmartTool) {
            return smartToolAvailable
        }
        return false
    }

    function handleToolModeChanged() {
        if (!canvas) {
            return
        }

        if (canvas.toolMode === LabelCanvasEnums.RectangleTool
                || canvas.toolMode === LabelCanvasEnums.PolygonTool
                || (canvas.toolMode === LabelCanvasEnums.SmartTool
                    && smartLayer && smartLayer.promptMode === LabelCanvasEnums.BoxPrompt)) {
            canvas.canvasMouseArea.cursorShape = Qt.CrossCursor
        } else {
            canvas.canvasMouseArea.cursorShape = Qt.ArrowCursor
        }

        if (polygonLayer && canvas.toolMode !== LabelCanvasEnums.PolygonTool) {
            polygonLayer.cancelDrawing()
        }
        if (smartLayer) {
            smartLayer.handleToolModeChanged()
        }
    }

    function handleCurrentImageChanged() {
        if (polygonLayer) {
            polygonLayer.cancelDrawing()
        }
        if (smartLayer) {
            smartLayer.handleCurrentImageChanged()
        }
    }

    function handleDrawingColorChanged() {
        if (visuals) {
            visuals.requestSmartOverlayPaint()
        }
        if (smartLayer) {
            smartLayer.handleDrawingColorChanged()
        }
        if (polygonLayer && polygonLayer.drawingPolygon) {
            polygonLayer.updatePreview()
        }
    }

    function handleToolKeyPressed(event) {
        if (smartLayer && smartLayer.handleKeyPressed(event)) {
            return true
        }
        if (shapeEditor && shapeEditor.handleKeyPressed(event)) {
            return true
        }
        if (event.key === Qt.Key_Escape && polygonToolMode && polygonLayer && polygonLayer.drawingPolygon) {
            polygonLayer.cancelDrawing()
            event.accepted = true
            return true
        }
        if (event.key === Qt.Key_Escape
                && polygonToolMode
                && canvas.interactionState === LabelCanvasEnums.Idle) {
            canvas.setToolMode(LabelCanvasEnums.SelectTool)
            event.accepted = true
            return true
        }
        return false
    }

    function handleToolMousePressed(event) {
        let pos = canvas.geometry.getPosOnImage(event)
        if (smartLayer && smartLayer.handleMousePressed(pos, event.button)) {
            event.accepted = true
            return true
        }
        if (polygonLayer && polygonLayer.handlePress(event, pos)) {
            if (event.button === Qt.RightButton) {
                canvas.suppressNextRelease = true
            }
            return true
        }
        if (event.button === Qt.LeftButton && shapeEditor) {
            shapeEditor.beginToolPress(pos)
            return true
        }
        return false
    }

    function handleToolMouseReleased(event) {
        if (consumeSuppressedRelease()) {
            return true
        }
        if (smartLayer && smartLayer.handleMouseReleased(canvas.geometry.getPosOnImage(event), event.button)) {
            return true
        }
        if (smartLayer && smartLayer.shouldConsumeIdleRelease(event.button)) {
            return true
        }
        if (polygonToolMode && polygonLayer && polygonLayer.drawingPolygon
                && canvas.interactionState === LabelCanvasEnums.Idle) {
            polygonLayer.updatePreview(canvas.geometry.getPosOnImage(event))
            return true
        }
        return shapeEditor ? shapeEditor.handleMouseReleased(event) : false
    }

    function handleToolMousePositionChanged(event) {
        let pos = canvas.geometry.getPosOnImage(event)
        if (smartLayer && smartLayer.handleMousePositionChanged(pos)) {
            return true
        }
        if (polygonToolMode && polygonLayer && polygonLayer.drawingPolygon
                && canvas.interactionState === LabelCanvasEnums.Idle) {
            polygonLayer.updatePreview(pos)
            return true
        }
        if ((rectangleToolMode || polygonToolMode) && canvas.interactionState === LabelCanvasEnums.Idle) {
            canvas.canvasMouseArea.cursorShape = Qt.CrossCursor
        }
        return shapeEditor ? shapeEditor.handleMousePositionChanged(event, pos) : false
    }

    function handleToolMouseDoubleClicked(event) {
        if (polygonLayer && polygonLayer.handleDoubleClicked(event)) {
            canvas.suppressNextRelease = true
            return true
        }
        if (selectToolMode && polygonRegionMode && event.button === Qt.LeftButton && polygonLayer) {
            let pos = canvas.geometry.getPosOnImage(event)
            if (polygonLayer.insertPointOnSelectedPolygonEdge(pos)) {
                canvas.interactionState = LabelCanvasEnums.Idle
                canvas.suppressNextRelease = true
                event.accepted = true
                return true
            }
        }
        return false
    }

    function consumeSuppressedRelease() {
        if (!canvas.suppressNextRelease) {
            return false
        }
        canvas.suppressNextRelease = false
        canvas.interactionState = LabelCanvasEnums.Idle
        return true
    }

    function addCurrentLabel(data) {
        if (!canvas || !canvas.dataManager || !imageInstances || !labelClasses) {
            console.warn("Add label failed: data manager, image model, or label class model is not ready")
            return false
        }

        let imageId = imageInstances.currentImageId
        let labelClassId = labelClasses.currentLabelClassId
        if (imageId === undefined || imageId < 0) {
            console.warn("Add label failed: current image is invalid", imageId)
            return false
        }
        if (labelClassId === undefined || labelClassId < 0) {
            console.warn("Add label failed: current label class is invalid", labelClassId)
            return false
        }

        let added = canvas.dataManager.addLabel(imageId, labelClassId, data)
        if (added && imageLevelLabelMode) {
            afterLabelAdded(imageId, labelClassId, data)
        }
        return added
    }
}
