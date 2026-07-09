import QtQuick

import dltool.core
import dltool.data

Item {
    id: visuals
    anchors.fill: parent

    property var canvas
    property ImageLabelsListModel imageLabelsList: canvas ? canvas.imageLabelsList : null
    property bool showBoundingBoxes: false
    property real labelFillOpacity: 0.3
    property bool smartActive: false
    property var smartResult: ({})
    property var smartPoints: []
    property var smartHoverPoint: ({})
    property bool smartHoverPointValid: false
    property real smartMaskAlpha: 0.35

    property alias labelsListView: labelsOverlay
    property alias drawingItem: drawingPreviewItem

    Connections {
        target: canvas ? canvas.imageView.image : null
        function onXChanged() { visuals.requestSmartOverlayPaint() }
        function onYChanged() { visuals.requestSmartOverlayPaint() }
        function onScaleChanged() { visuals.requestSmartOverlayPaint() }
        function onStatusChanged() { visuals.requestSmartOverlayPaint() }
    }

    LabelSmartMaskCanvas {
        id: smartMaskCanvas
        anchors.fill: parent
        active: visuals.smartActive
        imageItem: visuals.canvas ? visuals.canvas.imageView.image : null
        result: visuals.smartResult
        fillColor: visuals.canvas ? visuals.canvas.drawingColor : "red"
        maskAlpha: visuals.smartMaskAlpha
    }

    LabelsListView {
        id: labelsOverlay
        offsetX: visuals.canvas ? visuals.canvas.imageView.image.x : 0
        offsetY: visuals.canvas ? visuals.canvas.imageView.image.y : 0
        factor: visuals.canvas ? visuals.canvas.imageView.image.scale : 1
        showBoundingBoxes: visuals.showBoundingBoxes
        fillOpacity: visuals.labelFillOpacity
        model: visuals.canvas && visuals.canvas.imageView.image.status === Image.Ready ? visuals.imageLabelsList : null
    }

    DrawingItem {
        id: drawingPreviewItem
        offsetX: visuals.canvas ? visuals.canvas.imageView.image.x : 0
        offsetY: visuals.canvas ? visuals.canvas.imageView.image.y : 0
        factor: visuals.canvas ? visuals.canvas.imageView.image.scale : 1
        fillOpacity: visuals.labelFillOpacity
    }

    LabelSmartPromptCanvas {
        id: smartPromptCanvas
        anchors.fill: parent
        active: visuals.smartActive
        geometry: visuals.canvas ? visuals.canvas.geometry : null
        points: visuals.smartPoints
        hoverPoint: visuals.smartHoverPoint
        hoverPointValid: visuals.smartHoverPointValid
    }

    CrosshairCanvas {
        visible: visuals.canvas
                 && visuals.canvas.canvasMouseArea.containsMouse
                 && visuals.canvas.imageView.image.status === Image.Ready
                 && visuals.canvas.interactionState !== LabelCanvasEnums.Dragging
                 && visuals.canvas.interactionState !== LabelCanvasEnums.Editing
                 && (visuals.canvas.toolMode === LabelCanvasEnums.RectangleTool
                     || visuals.canvas.toolMode === LabelCanvasEnums.PolygonTool)
        mousePos: visuals.canvas ? Qt.point(visuals.canvas.canvasMouseArea.mouseX,
                                            visuals.canvas.canvasMouseArea.mouseY)
                                  : Qt.point(0, 0)
    }

    function requestSmartOverlayPaint() {
        smartMaskCanvas.requestPaint()
        smartPromptCanvas.requestPaint()
    }
}
