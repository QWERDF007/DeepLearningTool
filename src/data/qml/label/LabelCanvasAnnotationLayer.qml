import QtQuick
import QtQuick.Controls
import QtQml.Models

import dltool.data

Item {
    id: annotationLayer
    anchors.fill: parent

    property var canvas
    property bool rectangleToolAvailable: true
    property bool polygonRegionMode: false
    property bool imageLevelLabelMode: false
    property var afterLabelAdded: function(imageId, labelClassId, data) {}
    property bool showBoundingBoxes: false
    property int smartPromptMode: LabelCanvasEnums.PointPrompt
    property alias actions: labelCanvasActions

    readonly property ImageInstancesModel imageInstances: canvas ? canvas.imageInstances : null
    readonly property ImageLabelsListModel imageLabelsList: canvas ? canvas.imageLabelsList : null
    readonly property ItemSelectionModel selection: canvas ? canvas.selection : null
    readonly property bool smartAnnotationMode: inputRouter.smartAnnotationMode
    readonly property bool selectToolMode: inputRouter.selectToolMode
    readonly property bool rectangleToolMode: inputRouter.rectangleToolMode
    readonly property bool polygonToolMode: inputRouter.polygonToolMode
    readonly property bool smartToolAvailable: inputRouter.smartToolAvailable

    LabelCanvasAnnotationSettings {
        id: annotationSettings
        onSettingsUpdated: annotationVisuals.requestSmartOverlayPaint()
    }

    LabelCanvasContextMenu {
        id: labelCanvasActions
        dataManager: annotationLayer.canvas ? annotationLayer.canvas.dataManager : null
        featureManager: annotationLayer.canvas ? annotationLayer.canvas.featureManager : null
        imageInstances: annotationLayer.imageInstances
        imageLabelsList: annotationLayer.imageLabelsList
        selection: annotationLayer.selection
        imageSearch: annotationLayer.canvas && annotationLayer.canvas.featureManager
                     ? annotationLayer.canvas.featureManager.imageSearch
                     : null
        roiSearch: annotationLayer.canvas && annotationLayer.canvas.featureManager
                   ? annotationLayer.canvas.featureManager.roiSearch
                   : null
        roiSearchEnabled: annotationSettings.roiSearchEnabled
    }

    LabelCanvasAnnotationVisuals {
        id: annotationVisuals
        canvas: annotationLayer.canvas
        imageLabelsList: annotationLayer.imageLabelsList
        showBoundingBoxes: annotationLayer.showBoundingBoxes
        labelFillOpacity: annotationSettings.labelFillOpacity
        smartActive: smartLayer.active
        smartResult: smartLayer.result
        smartPoints: smartLayer.points
        smartHoverPoint: smartLayer.hoverPoint
        smartHoverPointValid: smartLayer.hoverPointValid
        smartBox: smartLayer.box
        smartBoxValid: smartLayer.boxValid
        smartMaskAlpha: annotationSettings.smartAnnotationMaskAlpha
    }

    LabelCanvasSelectionBridge {
        imageLabelsList: annotationLayer.imageLabelsList
        selection: annotationLayer.selection
    }

    LabelCanvasPolygonLayer {
        id: polygonLayer
        canvas: annotationLayer.canvas
        drawingItem: annotationVisuals.drawingItem
        active: inputRouter.polygonToolMode
        addLabelHandler: function(data) {
            return inputRouter.addCurrentLabel(data)
        }
        clearSelectionHandler: function() {
            shapeEditor.clearSelection()
        }
    }

    LabelCanvasSmartAnnotationLayer {
        id: smartLayer
        canvas: annotationLayer.canvas
        drawingItem: annotationVisuals.drawingItem
        segmentationMode: annotationLayer.polygonRegionMode
        drawingPolygon: polygonLayer.drawingPolygon
        refreshInterval: annotationSettings.smartAnnotationRefreshInterval
        promptMode: annotationLayer.smartPromptMode
        addLabelHandler: function(data) {
            return inputRouter.addCurrentLabel(data)
        }
    }

    LabelCanvasShapeEditor {
        id: shapeEditor
        canvas: annotationLayer.canvas
        labelsListView: annotationVisuals.labelsListView
        drawingItem: annotationVisuals.drawingItem
        actions: labelCanvasActions
        selectToolMode: inputRouter.selectToolMode
        rectangleToolMode: inputRouter.rectangleToolMode
        addLabelHandler: function(data) {
            return inputRouter.addCurrentLabel(data)
        }
    }

    LabelCanvasAnnotationInputRouter {
        id: inputRouter
        canvas: annotationLayer.canvas
        visuals: annotationVisuals
        smartLayer: smartLayer
        polygonLayer: polygonLayer
        shapeEditor: shapeEditor
        rectangleToolAvailable: annotationLayer.rectangleToolAvailable
        polygonRegionMode: annotationLayer.polygonRegionMode
        imageLevelLabelMode: annotationLayer.imageLevelLabelMode
        afterLabelAdded: annotationLayer.afterLabelAdded
    }

    Shortcut {
        enabled: canvas && canvas.visible && annotationLayer.rectangleToolAvailable
        sequence: "F2"
        onActivated: canvas.activateToolMode(LabelCanvasEnums.RectangleTool)
    }

    Shortcut {
        enabled: canvas && canvas.visible && annotationLayer.polygonRegionMode
        sequence: "F3"
        onActivated: canvas.activateToolMode(LabelCanvasEnums.PolygonTool)
    }

    Shortcut {
        enabled: canvas && canvas.visible && annotationLayer.smartToolAvailable
        sequence: "F4"
        onActivated: canvas.activateToolMode(LabelCanvasEnums.SmartTool)
    }
}
