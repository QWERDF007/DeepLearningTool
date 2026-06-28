import QtQuick
import QtQml.Models

import dltool.data

Item {
    id: polygonTool
    visible: false

    property bool active: false
    property bool drawingPolygon: false
    property var points: []
    property var geometry: null
    property var drawingItem: null
    property DataManager dataManager: null
    property ImageInstancesModel imageInstances: null
    property LabelClassesModel labelClasses: null
    property ImageLabelsListModel imageLabelsList: null
    property ItemSelectionModel selection: null
    property real imageScale: 1
    property color drawingColor: "red"

    signal addLabelRequested(var data)
    signal clearSelectionRequested()

    function handlePress(event, pos) {
        if (!active) {
            return false
        }
        if (event.button === Qt.RightButton && drawingPolygon) {
            finishDrawing()
            event.accepted = true
            return true
        }
        if (event.button === Qt.LeftButton) {
            appendPoint(pos)
            event.accepted = true
            return true
        }
        return false
    }

    function handleDoubleClicked(event) {
        if (active && event.button === Qt.LeftButton && drawingPolygon) {
            finishDrawing()
            event.accepted = true
            return true
        }
        return false
    }

    function appendPoint(pos) {
        if (!geometry) {
            return
        }

        let imagePos = geometry.clampPointToImage(pos)
        if (drawingPolygon && points.length >= 3) {
            let closeDistance = 10 / Math.max(imageScale, 0.01)
            if (geometry.distance(imagePos, points[0]) <= closeDistance) {
                finishDrawing()
                return
            }
        }

        if (!drawingPolygon) {
            clearSelectionRequested()
            points = []
            drawingPolygon = true
        }

        let updatedPoints = geometry.clonePoints(points)
        updatedPoints.push({x: imagePos.x, y: imagePos.y})
        points = updatedPoints
        updatePreview(imagePos)
    }

    function updatePreview(pos) {
        if (!drawingPolygon || !drawingItem || !geometry) {
            return
        }

        let previewPoints = geometry.clonePoints(points)
        if (pos && previewPoints.length > 0) {
            let imagePos = geometry.clampPointToImage(pos)
            if (geometry.distance(imagePos, previewPoints[previewPoints.length - 1]) > 0.0001) {
                previewPoints.push({x: imagePos.x, y: imagePos.y})
            }
        }
        drawingItem.updateItem({label_id: -1, points: previewPoints, color: drawingColor})
    }

    function finishDrawing() {
        if (!geometry) {
            cancelDrawing()
            return
        }

        let finalPoints = geometry.clonePoints(points)
        let bounds = geometry.boundsFromPoints(finalPoints)
        if (dataManager && imageInstances && labelClasses && labelClasses.currentLabelClassId !== -1
                && finalPoints.length >= 3 && bounds.width > 1 && bounds.height > 1) {
            addLabelRequested({
                                  points: finalPoints,
                                  x: bounds.x,
                                  y: bounds.y,
                                  width: bounds.width,
                                  height: bounds.height
                              })
        }
        cancelDrawing()
    }

    function cancelDrawing() {
        points = []
        drawingPolygon = false
        if (drawingItem) {
            drawingItem.clearItem()
        }
    }

    function insertPointOnSelectedPolygonEdge(pos) {
        if (!dataManager || !imageLabelsList || !selection || !selection.hasSelection || !geometry) {
            return false
        }

        let selectedIndex = imageLabelsList.getTopSelectedIndex()
        if (selectedIndex === -1) {
            return false
        }

        let hit = imageLabelsList.hitTestHandle(pos, selectedIndex, imageScale)
        if (!hit.found || hit.edge_index === undefined) {
            return false
        }

        let data = imageLabelsList.getData(selectedIndex)
        if (!data || data.label_id === undefined || data.label_id === -1
                || !data.points || data.points.length < 3) {
            return false
        }

        let updatedPoints = geometry.clonePoints(data.points)
        let edgeIndex = Number(hit.edge_index)
        if (isNaN(edgeIndex) || Math.floor(edgeIndex) !== edgeIndex
                || edgeIndex < 0 || edgeIndex >= updatedPoints.length) {
            return false
        }

        let imagePos = geometry.clampPointToImage(pos)
        updatedPoints.splice(edgeIndex + 1, 0, {x: imagePos.x, y: imagePos.y})

        let bounds = geometry.boundsFromPoints(updatedPoints)
        data.x = bounds.x
        data.y = bounds.y
        data.width = bounds.width
        data.height = bounds.height
        data.point_count = updatedPoints.length
        data.points = updatedPoints
        delete data.hit

        dataManager.updateLabels([data.label_id], [data])
        return true
    }
}
