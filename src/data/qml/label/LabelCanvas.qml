import QtQuick
import QtQml.Models

import dltool.ui
import dltool.core
import dltool.data
import dltool.feature
import dltool.settings
import quickui

Item {
    id: labelView
    clip: true

    property DataManager dataManager
    property FeatureManager featureManager
    property ImageInstancesModel imageInstances: dataManager ? dataManager.imageInstances : null
    property LabelClassesModel labelClasses: dataManager ? dataManager.labelClasses : null
    property ImageLabelsListModel imageLabelsList: dataManager ? dataManager.imageLabelsList : null
    property ItemSelectionModel selection: imageLabelsList ? imageLabelsList.selection : null
    property color drawingColor: labelClasses ? labelClasses.currentLabelClassColor : "red"
    property point startPos: Qt.point(0, 0)
    property bool segmentationMode: dataManager ? dataManager.method === DeepLearningMethod.Segmentation : false
    property ImageSearchController imageSearch: featureManager ? featureManager.imageSearch : null
    property RoiSearchController roiSearch: featureManager ? featureManager.roiSearch : null
    property SmartAnnotationController smartAnnotation: featureManager ? featureManager.smartAnnotation : null
    property bool roiSearchEnabled: true
    property int smartAnnotationRefreshInterval: 80
    property real smartAnnotationMaskAlpha: 0.35
    property int toolMode: LabelCanvasEnums.SelectTool
    property bool showBoundingBoxes: false
    property bool smartAnnotationMode: toolMode === LabelCanvasEnums.SmartTool && smartAnnotationAvailable
    property bool selectToolMode: toolMode === LabelCanvasEnums.SelectTool
    property bool rectangleToolMode: toolMode === LabelCanvasEnums.RectangleTool
    property bool polygonToolMode: toolMode === LabelCanvasEnums.PolygonTool && segmentationMode
    readonly property bool smartAnnotationAvailable: smartAnnotationController.available
    property real imageScale: labelImage.image.scale

    onToolModeChanged: handleToolModeChanged()
    onSmartAnnotationAvailableChanged: handleSmartAnnotationAvailableChanged()
    onSegmentationModeChanged: {
        if (!segmentationMode && toolMode === LabelCanvasEnums.PolygonTool) {
            setToolMode(LabelCanvasEnums.SelectTool)
        }
    }
    onDrawingColorChanged: {
        smartMaskCanvas.requestPaint()
        smartAnnotationController.refreshDrawingPreview()
        if (polygonTool.drawingPolygon) {
            polygonTool.updatePreview()
        }
    }

    Connections {
        target: SignalHelper
        function onImageLabelTableSelectionChanged(index, command) {
            if (selection) {
                selection.select(index, command)
            }
        }
        function onImageLabelTableShiftSelect(currentIndex, lastIndex, command) {
            if (imageLabelsList) {
                imageLabelsList.shiftSelect(currentIndex, lastIndex, command)
            }
        }
        function onImageLabelTableSelectAll() {
            if (imageLabelsList) {
                imageLabelsList.selectAll()
            }
        }
        function onImageLabelTableSelectionClear() {
            if (selection) {
                selection.clear()
            }
        }
    }

    Connections {
        target: imageInstances
        function onCurrentImageChanged() {
            polygonTool.cancelDrawing()
            smartAnnotationController.clear()
        }
    }

    LabelCanvasGeometry {
        id: canvasGeometry
        imageItem: labelImage.image
    }

    LabelSmartAnnotationController {
        id: smartAnnotationController
        dataManager: labelView.dataManager
        imageInstances: labelView.imageInstances
        labelClasses: labelView.labelClasses
        smartAnnotation: labelView.smartAnnotation
        geometry: canvasGeometry
        drawingItem: drawingItem
        active: labelView.smartAnnotationMode
        segmentationMode: labelView.segmentationMode
        drawingPolygon: polygonTool.drawingPolygon
        drawingColor: labelView.drawingColor
        refreshInterval: labelView.smartAnnotationRefreshInterval
    }

    LabelPolygonTool {
        id: polygonTool
        active: labelView.polygonToolMode
        geometry: canvasGeometry
        drawingItem: drawingItem
        dataManager: labelView.dataManager
        imageInstances: labelView.imageInstances
        labelClasses: labelView.labelClasses
        imageLabelsList: labelView.imageLabelsList
        selection: labelView.selection
        imageScale: labelImage.image.scale
        drawingColor: labelView.drawingColor
        onAddLabelRequested: function(data) {
            labelView.addCurrentLabel(data)
        }
        onClearSelectionRequested: labelView.clearSelection()
    }

    LabelCanvasContextMenu {
        id: labelCanvasActions
        dataManager: labelView.dataManager
        featureManager: labelView.featureManager
        imageInstances: labelView.imageInstances
        imageLabelsList: labelView.imageLabelsList
        selection: labelView.selection
        imageSearch: labelView.imageSearch
        roiSearch: labelView.roiSearch
        roiSearchEnabled: labelView.roiSearchEnabled
    }

    LabelImage {
        id: labelImage
        anchors.fill: parent
        currentImagePath: imageInstances ? imageInstances.currentImagePath : ""
    }

    LabelSmartMaskCanvas {
        id: smartMaskCanvas
        anchors.fill: parent
        active: labelView.smartAnnotationMode
        imageItem: labelImage.image
        result: smartAnnotationController.result
        fillColor: labelView.drawingColor
        maskAlpha: labelView.smartAnnotationMaskAlpha
    }

    LabelsListView {
        id: labelsListView
        offsetX: labelImage.image.x
        offsetY: labelImage.image.y
        factor: labelImage.image.scale
        showBoundingBoxes: labelView.showBoundingBoxes
        model: labelImage.image.status === Image.Ready ? imageLabelsList : null
    }

    DrawingItem {
        id: drawingItem
        offsetX: labelImage.image.x
        offsetY: labelImage.image.y
        factor: labelImage.image.scale
    }

    LabelSmartPromptCanvas {
        id: smartPromptCanvas
        anchors.fill: parent
        active: labelView.smartAnnotationMode
        geometry: canvasGeometry
        points: smartAnnotationController.points
        hoverPoint: smartAnnotationController.hoverPoint
        hoverPointValid: smartAnnotationController.hoverPointValid
    }

    Connections {
        target: labelImage.image
        function onXChanged() { requestSmartOverlayPaint() }
        function onYChanged() { requestSmartOverlayPaint() }
        function onScaleChanged() { requestSmartOverlayPaint() }
        function onStatusChanged() { requestSmartOverlayPaint() }
    }

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            labelView.refreshSettings()
            smartMaskCanvas.requestPaint()
        }
    }

    CrosshairCanvas {
        visible: mouseArea.containsMouse
                 && labelImage.image.status === Image.Ready
                 && mouseArea.interactionState !== LabelCanvasEnums.Dragging
                 && mouseArea.interactionState !== LabelCanvasEnums.Editing
        mousePos: Qt.point(mouseArea.mouseX, mouseArea.mouseY)
    }

    Keys.onPressed: function(event) { handleKeyPressed(event) }
    Keys.onReleased: function(event) { handleKeyReleased(event) }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        property int interactionState: LabelCanvasEnums.Idle
        property var activeData: null
        property bool suppressNextRelease: false

        onPressed: function(event) { handleMousePressed(event) }
        onReleased: function(event) { handleMouseReleased(event) }
        onPositionChanged: function(event) { handleMousePositionChanged(event) }
        onEntered: labelView.forceActiveFocus()
        onExited: handleMouseExited()
        onDoubleClicked: function(event) { handleMouseDoubleClicked(event) }
    }

    function handleToolModeChanged() {
        mouseArea.interactionState = LabelCanvasEnums.Idle
        mouseArea.cursorShape = Qt.ArrowCursor
        if (toolMode !== LabelCanvasEnums.PolygonTool) {
            polygonTool.cancelDrawing()
        }
        if (toolMode !== LabelCanvasEnums.SmartTool) {
            smartAnnotationController.clear()
        } else if (mouseArea.containsMouse) {
            smartAnnotationController.setHoverPoint(getPosOnImagePoint(mouseArea.mouseX, mouseArea.mouseY), true)
        }
    }

    function handleSmartAnnotationAvailableChanged() {
        if (!smartAnnotationAvailable && toolMode === LabelCanvasEnums.SmartTool) {
            toolMode = LabelCanvasEnums.SelectTool
        }
        if (!smartAnnotationAvailable) {
            smartAnnotationController.clear()
        }
    }

    function requestSmartOverlayPaint() {
        smartMaskCanvas.requestPaint()
        smartPromptCanvas.requestPaint()
    }

    function handleKeyPressed(event) {
        if (smartAnnotationMode && event.key === Qt.Key_Escape) {
            smartAnnotationController.clear()
            event.accepted = true
            return
        }
        if (smartAnnotationMode && isConfirmKey(event.key)) {
            confirmSmartAnnotation()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Control && !mouseArea.pressed) {
            mouseArea.cursorShape = Qt.OpenHandCursor
            return
        }
        if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
            if (imageLabelsList) {
                imageLabelsList.selectAll()
                SignalHelper.imageLabelListSelectAll()
            }
            return
        }
        if (event.key === Qt.Key_Delete && selection && selection.hasSelection) {
            deleteSelectedLabels()
            return
        }
        if (event.key === Qt.Key_Escape && polygonToolMode && polygonTool.drawingPolygon) {
            polygonTool.cancelDrawing()
            event.accepted = true
            return
        }
        handleLabelClassShortcut(event)
    }

    function handleKeyReleased(event) {
        if (event.key === Qt.Key_Control && !mouseArea.pressed) {
            mouseArea.cursorShape = Qt.ArrowCursor
        }
    }

    function isConfirmKey(key) {
        return key === Qt.Key_Space || key === Qt.Key_Return || key === Qt.Key_Enter
    }

    function handleLabelClassShortcut(event) {
        if (!labelClasses || !event.text || event.text.length <= 0) {
            return false
        }
        if (labelClasses.selectByShortcut(event.text)) {
            event.accepted = true
            return true
        }
        return false
    }

    function handleMousePressed(event) {
        if (!isIdle()) {
            return
        }
        if (isPanPress(event)) {
            beginImageDrag(event)
            return
        }
        if (smartAnnotationController.handlePress(getPosOnImage(event), event.button)) {
            event.accepted = true
            return
        }
        if (polygonTool.handlePress(event, getPosOnImage(event))) {
            if (event.button === Qt.RightButton) {
                mouseArea.suppressNextRelease = true
            }
            return
        }
        if (event.button === Qt.LeftButton) {
            beginToolPress(event)
        }
    }

    function handleMouseReleased(event) {
        if (consumeSuppressedRelease()) {
            return
        }
        if (isSmartAnnotationPointRelease(event)) {
            return
        }
        if (isPolygonPreviewRelease()) {
            polygonTool.updatePreview(getPosOnImage(event))
            return
        }
        if (isIgnoredNonSelectRelease()) {
            setIdleCursor(event.modifiers)
            mouseArea.interactionState = LabelCanvasEnums.Idle
            return
        }

        if (mouseArea.interactionState === LabelCanvasEnums.Drawing) {
            finishRectangleDrawing()
        } else if (mouseArea.interactionState === LabelCanvasEnums.Dragging) {
            finishImageDrag(event)
        } else if (mouseArea.interactionState === LabelCanvasEnums.Editing) {
            finishLabelEditing(event)
        } else if (mouseArea.interactionState === LabelCanvasEnums.ReadyEdit) {
            finishReadyEdit(event)
        } else {
            handleIdleRelease(event)
        }
        mouseArea.interactionState = LabelCanvasEnums.Idle
    }

    function handleMousePositionChanged(event) {
        if (smartAnnotationMode && isIdle()) {
            smartAnnotationController.handleHover(getPosOnImage(event), true)
            return
        }
        if (polygonToolMode && polygonTool.drawingPolygon && isIdle()) {
            polygonTool.updatePreview(getPosOnImage(event))
            return
        }

        if (selectToolMode && mouseArea.interactionState === LabelCanvasEnums.ReadyEdit) {
            beginLabelEditing(event)
        } else if (mouseArea.interactionState === LabelCanvasEnums.ReadyDraw) {
            beginRectangleDrawing()
        }

        updateActiveMouseState(event)
    }

    function handleMouseExited() {
        smartAnnotationController.handleExited()
    }

    function handleMouseDoubleClicked(event) {
        if (polygonTool.handleDoubleClicked(event)) {
            mouseArea.suppressNextRelease = true
            return
        }
        if (selectToolMode && segmentationMode && event.button === Qt.LeftButton) {
            let pos = getPosOnImage(event)
            if (polygonTool.insertPointOnSelectedPolygonEdge(pos)) {
                mouseArea.interactionState = LabelCanvasEnums.Idle
                mouseArea.suppressNextRelease = true
                event.accepted = true
            }
        }
    }

    function isIdle() {
        return mouseArea.interactionState === LabelCanvasEnums.Idle
    }

    function isPanPress(event) {
        return event.button === Qt.MiddleButton
               || ((event.modifiers & Qt.ControlModifier) && event.button === Qt.LeftButton)
    }

    function isPrimaryOrSecondaryButton(event) {
        return event.button === Qt.LeftButton || event.button === Qt.RightButton
    }

    function beginImageDrag(event) {
        mouseArea.cursorShape = Qt.ClosedHandCursor
        startPos = Qt.point(event.x, event.y)
        mouseArea.interactionState = LabelCanvasEnums.Dragging
    }

    function beginToolPress(event) {
        startPos = getPosOnImage(event)
        if (rectangleToolMode) {
            mouseArea.interactionState = LabelCanvasEnums.ReadyDraw
            clearSelection()
        } else if (selectToolMode) {
            mouseArea.interactionState = hitTest(startPos) ? LabelCanvasEnums.ReadyEdit : LabelCanvasEnums.Idle
        } else {
            mouseArea.interactionState = LabelCanvasEnums.Idle
        }
        if (imageLabelsList) {
            imageLabelsList.setHovered([])
        }
    }

    function consumeSuppressedRelease() {
        if (!mouseArea.suppressNextRelease) {
            return false
        }
        mouseArea.suppressNextRelease = false
        mouseArea.interactionState = LabelCanvasEnums.Idle
        return true
    }

    function isSmartAnnotationPointRelease(event) {
        return smartAnnotationMode && isIdle() && isPrimaryOrSecondaryButton(event)
    }

    function isPolygonPreviewRelease() {
        return polygonToolMode && polygonTool.drawingPolygon && isIdle()
    }

    function isIgnoredNonSelectRelease() {
        return !selectToolMode
               && mouseArea.interactionState !== LabelCanvasEnums.Drawing
               && mouseArea.interactionState !== LabelCanvasEnums.Dragging
    }

    function setIdleCursor(modifiers) {
        mouseArea.cursorShape = (modifiers & Qt.ControlModifier) ? Qt.OpenHandCursor : Qt.ArrowCursor
    }

    function finishRectangleDrawing() {
        mouseArea.activeData = drawingItem.getData()
        if (mouseArea.activeData.width > 1 && mouseArea.activeData.height > 1) {
            addCurrentLabel(mouseArea.activeData)
        }
        drawingItem.clearItem()
    }

    function finishImageDrag(event) {
        setIdleCursor(event.modifiers)
        startPos = Qt.point(event.x, event.y)
    }

    function finishLabelEditing(event) {
        let item = labelsListView.itemAt(mouseArea.activeData.index)
        if (dataManager && mouseArea.activeData.label_id !== -1) {
            dataManager.updateLabels([mouseArea.activeData.label_id], [mouseArea.activeData])
        }
        let pos = getPosOnImage(event)
        if (!hitTest(pos)) {
            mouseArea.cursorShape = Qt.ArrowCursor
        }
        if (item) {
            item.visible = true
        }
        drawingItem.clearItem()
    }

    function finishReadyEdit(event) {
        let pos = getPosOnImage(event)
        let hit = hitTest(pos)
        if (isEditHandleHit(hit)) {
            return
        }
        if (event.button === Qt.LeftButton) {
            setIdleCursor(event.modifiers)
            selectAt(pos)
        }
    }

    function handleIdleRelease(event) {
        setIdleCursor(event.modifiers)
        let pos = getPosOnImage(event)
        if (event.button === Qt.LeftButton) {
            selectAt(pos)
        } else if (event.button === Qt.RightButton) {
            openContextMenuAt(pos)
        }
    }

    function selectAt(pos) {
        if (!imageLabelsList) {
            return
        }
        let indices = imageLabelsList.getIndicesAt(pos)
        let newIndex = imageLabelsList.chooseIndex(indices)
        select(newIndex, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
        hitTest(pos)
    }

    function openContextMenuAt(pos) {
        if (!imageLabelsList) {
            return
        }
        let indices = imageLabelsList.getIndicesAt(pos)
        if (!hasSelectedIndex(indices)) {
            let newIndex = imageLabelsList.chooseIndex(indices)
            select(newIndex, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
        }
        labelCanvasActions.popup()
    }

    function hasSelectedIndex(indices) {
        if (!selection || !imageLabelsList) {
            return false
        }
        for (let index of indices) {
            if (selection.isSelected(imageLabelsList.index(index, 0))) {
                return true
            }
        }
        return false
    }

    function beginLabelEditing(event) {
        mouseArea.interactionState = LabelCanvasEnums.Editing
        let pos = getPosOnImage(event)
        let hit = hitTest(pos)
        let index = imageLabelsList.getTopSelectedIndex()
        let item = labelsListView.itemAt(index)
        if (item) {
            item.visible = false
        }
        mouseArea.activeData = imageLabelsList.getData(index)
        mouseArea.activeData.hit = hit
        drawingItem.initItem(mouseArea.activeData)
    }

    function beginRectangleDrawing() {
        mouseArea.interactionState = LabelCanvasEnums.Drawing
        drawingItem.initItem(newDraftLabelData({x: startPos.x, y: startPos.y, width: 0, height: 0}))
    }

    function updateActiveMouseState(event) {
        if (mouseArea.interactionState === LabelCanvasEnums.Drawing) {
            updateRectangleDrawing(event)
        } else if (mouseArea.interactionState === LabelCanvasEnums.Dragging) {
            moveImage(event)
        } else if (mouseArea.interactionState === LabelCanvasEnums.Editing) {
            updateLabelEditing(event)
        } else if (selectToolMode) {
            updateSelectionHover(event)
        } else if (imageLabelsList) {
            imageLabelsList.setHovered([])
        }
    }

    function updateRectangleDrawing(event) {
        let rect = canvasGeometry.rectFromPoints(startPos, getPosOnImage(event))
        drawingItem.updateItem(newDraftLabelData(rect))
    }

    function updateLabelEditing(event) {
        let endPos = getPosOnImage(event)
        mouseArea.activeData = imageLabelsList.getEditedData(mouseArea.activeData, startPos, endPos)
        drawingItem.updateItem(mouseArea.activeData)
        startPos = endPos
    }

    function updateSelectionHover(event) {
        if (!imageLabelsList) {
            return
        }
        let pos = getPosOnImage(event)
        if (!hitTest(pos)) {
            mouseArea.cursorShape = Qt.ArrowCursor
            imageLabelsList.setHovered(imageLabelsList.getIndicesAt(pos))
        }
    }

    function isEditHandleHit(hit) {
        return hit && hit.found && (hit.edge_index !== undefined || hit.mode === 1)
    }

    function confirmSmartAnnotation() {
        let data = smartAnnotationController.confirm(mouseArea.containsMouse, mouseArea.mouseX, mouseArea.mouseY)
        if (data && addCurrentLabel(data)) {
            smartAnnotationController.clear()
        }
    }

    function updateSmartAnnotationPreview() {
        smartAnnotationController.updatePreview()
    }

    function clearSmartAnnotation() {
        smartAnnotationController.clear()
    }

    function cancelPolygonDrawing() {
        polygonTool.cancelDrawing()
    }

    function select(index, command) {
        if (selection) {
            selection.select(index, command)
            SignalHelper.imageLabelListSelectionChanged(index, command)
        }
    }

    function shiftSelect(currentIndex, lastIndex, command) {
        if (selection && imageLabelsList) {
            imageLabelsList.shiftSelect(currentIndex, lastIndex, command)
            SignalHelper.imageLabelListShiftSelect(currentIndex, lastIndex, command)
        }
    }

    function clearSelection() {
        if (selection) {
            selection.clear()
            SignalHelper.imageLabelListSelectionClear()
        }
    }

    function addCurrentLabel(data) {
        if (!dataManager || !imageInstances || !labelClasses) {
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

        return dataManager.addLabel(imageId, labelClassId, data)
    }

    function setToolMode(mode) {
        if (mode === LabelCanvasEnums.SmartTool && !smartAnnotationAvailable) {
            mode = LabelCanvasEnums.SelectTool
        }
        if (mode === LabelCanvasEnums.PolygonTool && !segmentationMode) {
            mode = LabelCanvasEnums.SelectTool
        }
        toolMode = mode
    }

    function deleteSelectedLabels() {
        labelCanvasActions.deleteSelectedLabels()
    }

    function copySelectedLabels() {
        if (dataManager && selection && selection.hasSelection) {
            dataManager.duplicateSelectedLabels()
        }
    }

    function startImageSearchForCurrentImage() {
        labelCanvasActions.startImageSearchForCurrentImage()
    }

    function startRoiSearchForSelectedLabels() {
        labelCanvasActions.startRoiSearchForSelectedLabels()
    }

    function moveImage(event) {
        let dx = event.x - startPos.x
        let dy = event.y - startPos.y
        labelImage.image.x += dx
        labelImage.image.y += dy
        startPos = Qt.point(event.x, event.y)
    }

    function getPosOnImage(event) {
        return canvasGeometry.getPosOnImage(event)
    }

    function getPosOnImagePoint(x, y) {
        return canvasGeometry.getPosOnImagePoint(x, y)
    }

    function hitTest(pos) {
        if (!selectToolMode || !imageLabelsList) {
            return null
        }
        if (selection === null || !selection.hasSelection) {
            return null
        }

        let selectedIndex = imageLabelsList.getTopSelectedIndex()
        if (selectedIndex !== -1) {
            let hit = imageLabelsList.hitTestHandle(pos, selectedIndex, labelImage.image.scale)
            if (hit.found) {
                mouseArea.cursorShape = hit.cursor
                return hit
            }
        }
        return null
    }

    function fitImageInView() {
        labelImage.fitInView()
    }

    function setImageScale(scale) {
        labelImage.scaleInCenter(scale)
    }

    function refreshSettings() {
        roiSearchEnabled = GlobalSettings.valueForField(SettingsAccessor.RoiSearch, RoiSearchField.Enabled, true)
        smartAnnotationRefreshInterval = GlobalSettings.valueForField(
                    SettingsAccessor.SmartAnnotation,
                    SmartAnnotationField.RefreshInterval,
                    80)
        smartAnnotationMaskAlpha = GlobalSettings.valueForField(
                    SettingsAccessor.SmartAnnotation,
                    SmartAnnotationField.MaskAlpha,
                    0.35)
    }

    function newDraftLabelData(fields) {
        let data = {label_id: -1, color: drawingColor}
        for (let key in fields) {
            data[key] = fields[key]
        }
        return data
    }

    Component.onCompleted: refreshSettings()
}
