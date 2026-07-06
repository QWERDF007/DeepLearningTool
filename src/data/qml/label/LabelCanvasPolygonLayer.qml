import QtQuick
import QtQml.Models

import dltool.data

Item {
    id: polygonLayer
    visible: false

    property var canvas
    property var drawingItem
    property bool active: false
    property var addLabelHandler: function(data) { return false }
    property var clearSelectionHandler: function() {}
    property alias drawingPolygon: polygonTool.drawingPolygon

    readonly property ImageInstancesModel imageInstances: canvas ? canvas.imageInstances : null
    readonly property LabelClassesModel labelClasses: canvas ? canvas.labelClasses : null
    readonly property ImageLabelsListModel imageLabelsList: canvas ? canvas.imageLabelsList : null
    readonly property ItemSelectionModel selection: canvas ? canvas.selection : null

    LabelPolygonTool {
        id: polygonTool
        active: polygonLayer.active
        geometry: polygonLayer.canvas ? polygonLayer.canvas.geometry : null
        drawingItem: polygonLayer.drawingItem
        dataManager: polygonLayer.canvas ? polygonLayer.canvas.dataManager : null
        imageInstances: polygonLayer.imageInstances
        labelClasses: polygonLayer.labelClasses
        imageLabelsList: polygonLayer.imageLabelsList
        selection: polygonLayer.selection
        imageScale: polygonLayer.canvas ? polygonLayer.canvas.imageView.image.scale : 1
        drawingColor: polygonLayer.canvas ? polygonLayer.canvas.drawingColor : "red"
        onAddLabelRequested: function(data) {
            polygonLayer.addLabelHandler(data)
        }
        onClearSelectionRequested: polygonLayer.clearSelectionHandler()
    }

    function handlePress(event, pos) {
        return polygonTool.handlePress(event, pos)
    }

    function handleDoubleClicked(event) {
        return polygonTool.handleDoubleClicked(event)
    }

    function updatePreview(pos) {
        polygonTool.updatePreview(pos)
    }

    function cancelDrawing() {
        polygonTool.cancelDrawing()
    }

    function insertPointOnSelectedPolygonEdge(pos) {
        return polygonTool.insertPointOnSelectedPolygonEdge(pos)
    }
}
