import QtQuick
import QtQuick.Controls
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
    readonly property ImageInstancesModel imageInstances: dataManager ? dataManager.imageInstances : null
    readonly property LabelClassesModel labelClasses: dataManager ? dataManager.labelClasses : null
    readonly property ImageLabelsListModel imageLabelsList: dataManager ? dataManager.imageLabelsList : null
    readonly property ItemSelectionModel selection: imageLabelsList ? imageLabelsList.selection : null
    readonly property color drawingColor: labelClasses ? labelClasses.currentLabelClassColor : "red"
    property point startPos: Qt.point(0, 0)
    readonly property bool segmentationMode: dataManager ? dataManager.method === DeepLearningMethod.Segmentation : false
    readonly property bool classificationMode: dataManager ? dataManager.method === DeepLearningMethod.Classification : false
    property bool roiSearchEnabled: true
    property int smartAnnotationRefreshInterval: 80
    property real smartAnnotationMaskAlpha: 0.35
    property real labelFillOpacity: 0.3
    property int toolMode: LabelCanvasEnums.SelectTool
    property bool showBoundingBoxes: false
    readonly property bool smartAnnotationMode: toolMode === LabelCanvasEnums.SmartTool && smartAnnotationAvailable
    readonly property bool selectToolMode: toolMode === LabelCanvasEnums.SelectTool
    readonly property bool rectangleToolMode: toolMode === LabelCanvasEnums.RectangleTool
    readonly property bool polygonToolMode: toolMode === LabelCanvasEnums.PolygonTool && segmentationMode
    readonly property bool smartAnnotationAvailable: smartAnnotationController.available
    property var classificationBadgeData: null
    property alias imageView: labelImage
    property alias actions: labelCanvasActions

    onToolModeChanged: {
        mouseArea.interactionState = LabelCanvasEnums.Idle
        mouseArea.cursorShape = Qt.ArrowCursor
        if (toolMode !== LabelCanvasEnums.PolygonTool) {
            polygonTool.cancelDrawing()
        }
        if (toolMode !== LabelCanvasEnums.SmartTool) {
            smartAnnotationController.clear()
        } else if (mouseArea.containsMouse) {
            smartAnnotationController.setHoverPoint(canvasGeometry.getPosOnImagePoint(mouseArea.mouseX, mouseArea.mouseY), true)
        }
    }
    onSmartAnnotationAvailableChanged: {
        if (!smartAnnotationAvailable && toolMode === LabelCanvasEnums.SmartTool) {
            setToolMode(LabelCanvasEnums.SelectTool)
        }
        if (!smartAnnotationAvailable) {
            smartAnnotationController.clear()
        }
    }
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
            refreshClassificationBadge()
        }
    }

    Connections {
        target: imageLabelsList
        function onRowsInserted(parent, first, last) { refreshClassificationBadge() }
        function onRowsRemoved(parent, first, last) { refreshClassificationBadge() }
        function onDataChanged(topLeft, bottomRight, roles) { refreshClassificationBadge() }
        function onModelReset() { refreshClassificationBadge() }
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
        smartAnnotation: labelView.featureManager ? labelView.featureManager.smartAnnotation : null
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
        imageSearch: labelView.featureManager ? labelView.featureManager.imageSearch : null
        roiSearch: labelView.featureManager ? labelView.featureManager.roiSearch : null
        roiSearchEnabled: labelView.roiSearchEnabled
    }

    LabelImage {
        id: labelImage
        anchors.fill: parent
        currentImagePath: imageInstances ? imageInstances.currentImagePath : ""
    }

    Rectangle {
        id: classificationBadge
        visible: labelView.classificationMode
                 && labelImage.image.status === Image.Ready
                 && labelView.classificationBadgeData !== null
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 4
        width: Math.max(56, badgeText.implicitWidth + 14)
        height: Math.max(22, badgeText.implicitHeight + 6)
        radius: 2
        color: labelView.classificationBadgeData ? labelView.classificationBadgeData.color : labelView.drawingColor
        border.color: Qt.rgba(1, 1, 1, 0.18)
        border.width: 1
        z: 30

        QuiText {
            id: badgeText
            anchors.fill: parent
            anchors.leftMargin: 7
            anchors.rightMargin: 7
            anchors.topMargin: 2
            anchors.bottomMargin: 2
            text: labelView.classificationBadgeData ? String(labelView.classificationBadgeData.label_class_name ?? "") : ""
            textColor: "white"
            font.pixelSize: 12
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
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
        fillOpacity: labelView.labelFillOpacity
        model: !labelView.classificationMode && labelImage.image.status === Image.Ready ? imageLabelsList : null
    }

    DrawingItem {
        id: drawingItem
        offsetX: labelImage.image.x
        offsetY: labelImage.image.y
        factor: labelImage.image.scale
        fillOpacity: labelView.labelFillOpacity
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

    Connections {
        target: smartAnnotationController.smartAnnotation
        function onModelLoadFinished(success) {
            if (success && labelView.smartAnnotationMode) {
                smartAnnotationController.updatePreview()
            }
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
    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Control && !mouseArea.pressed) {
            mouseArea.cursorShape = Qt.ArrowCursor
        }
    }

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
        onCanceled: function() { resetDragState() }
        onPositionChanged: function(event) { handleMousePositionChanged(event) }
        onEntered: labelView.forceActiveFocus()
        onExited: smartAnnotationController.handleExited()
        onDoubleClicked: function(event) { handleMouseDoubleClicked(event) }
    }

    Shortcut {
        enabled: labelView.visible
        sequence: "F1"
        onActivated: labelView.activateToolMode(LabelCanvasEnums.SelectTool)
    }

    Shortcut {
        enabled: labelView.visible
        sequence: "F2"
        onActivated: labelView.activateToolMode(LabelCanvasEnums.RectangleTool)
    }

    Shortcut {
        enabled: labelView.visible && labelView.segmentationMode
        sequence: "F3"
        onActivated: labelView.activateToolMode(LabelCanvasEnums.PolygonTool)
    }

    Shortcut {
        enabled: labelView.visible && labelView.smartAnnotationAvailable
        sequence: "F4"
        onActivated: labelView.activateToolMode(LabelCanvasEnums.SmartTool)
    }

    function requestSmartOverlayPaint() {
        smartMaskCanvas.requestPaint()
        smartPromptCanvas.requestPaint()
    }

    function refreshClassificationBadge() {
        if (!classificationMode || !imageLabelsList || imageLabelsList.rowCount() <= 0) {
            classificationBadgeData = null
            return
        }

        classificationBadgeData = imageLabelsList.getData(0)
    }

    function handleKeyPressed(event) {
        if (handleToolShortcut(event)) {
            return
        }
        if (smartAnnotationMode && event.key === Qt.Key_Escape) {
            if (smartAnnotationController.points.length > 0) {
                smartAnnotationController.clear()
            } else {
                setToolMode(LabelCanvasEnums.SelectTool)
            }
            event.accepted = true
            return
        }
        if (smartAnnotationMode
                && (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            let data = smartAnnotationController.confirm(mouseArea.containsMouse, mouseArea.mouseX, mouseArea.mouseY)
            if (data && addCurrentLabel(data)) {
                smartAnnotationController.clear()
            }
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
            labelCanvasActions.deleteSelectedLabels()
            return
        }
        if (event.key === Qt.Key_Escape && polygonToolMode && polygonTool.drawingPolygon) {
            polygonTool.cancelDrawing()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Escape
                && (rectangleToolMode || polygonToolMode)
                && mouseArea.interactionState === LabelCanvasEnums.Idle) {
            setToolMode(LabelCanvasEnums.SelectTool)
            event.accepted = true
            return
        }
        handleLabelClassShortcut(event)
    }

    function handleToolShortcut(event) {
        if (event.modifiers !== Qt.NoModifier) {
            return false
        }

        let mode = -1
        if (event.key === Qt.Key_F1) {
            mode = LabelCanvasEnums.SelectTool
        } else if (event.key === Qt.Key_F2) {
            mode = LabelCanvasEnums.RectangleTool
        } else if (event.key === Qt.Key_F3) {
            mode = LabelCanvasEnums.PolygonTool
        } else if (event.key === Qt.Key_F4) {
            mode = LabelCanvasEnums.SmartTool
        } else {
            return false
        }

        if (activateToolMode(mode)) {
            event.accepted = true
            return true
        }
        return false
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
        if (mouseArea.interactionState !== LabelCanvasEnums.Idle) {
            return
        }
        if (event.button === Qt.MiddleButton
                || ((event.modifiers & Qt.ControlModifier) && event.button === Qt.LeftButton)) {
            mouseArea.cursorShape = Qt.ClosedHandCursor
            startPos = Qt.point(event.x, event.y)
            mouseArea.interactionState = LabelCanvasEnums.Dragging
            return
        }
        if (classificationMode) {
            return
        }
        let pos = canvasGeometry.getPosOnImage(event)
        if (smartAnnotationController.handlePress(pos, event.button)) {
            event.accepted = true
            return
        }
        if (polygonTool.handlePress(event, pos)) {
            if (event.button === Qt.RightButton) {
                mouseArea.suppressNextRelease = true
            }
            return
        }
        if (event.button === Qt.LeftButton) {
            beginToolPress(pos)
        }
    }

    function handleMouseReleased(event) {
        if (consumeSuppressedRelease()) {
            return
        }
        if (mouseArea.interactionState === LabelCanvasEnums.Dragging) {
            finishImageDragging(event)
            return
        }
        if (classificationMode) {
            return
        }
        if (smartAnnotationMode && mouseArea.interactionState === LabelCanvasEnums.Idle
                && (event.button === Qt.LeftButton || event.button === Qt.RightButton)) {
            return
        }
        if (polygonToolMode && polygonTool.drawingPolygon && mouseArea.interactionState === LabelCanvasEnums.Idle) {
            polygonTool.updatePreview(canvasGeometry.getPosOnImage(event))
            return
        }
        if (!selectToolMode
                && mouseArea.interactionState !== LabelCanvasEnums.Drawing
                && mouseArea.interactionState !== LabelCanvasEnums.Dragging) {
            setIdleCursor(event.modifiers)
            mouseArea.interactionState = LabelCanvasEnums.Idle
            return
        }

        if (mouseArea.interactionState === LabelCanvasEnums.Drawing) {
            finishRectangleDrawing()
        } else if (mouseArea.interactionState === LabelCanvasEnums.Editing) {
            finishLabelEditing(event)
        } else if (mouseArea.interactionState === LabelCanvasEnums.ReadyEdit) {
            finishReadyEdit(event)
        } else {
            handleIdleRelease(event)
        }
        mouseArea.interactionState = LabelCanvasEnums.Idle
    }

    function finishImageDragging(event) {
        setIdleCursor(event.modifiers)
        startPos = Qt.point(event.x, event.y)
        mouseArea.interactionState = LabelCanvasEnums.Idle
    }

    function handleMousePositionChanged(event) {
        if (classificationMode && mouseArea.interactionState === LabelCanvasEnums.Idle) {
            return
        }
        let pos = canvasGeometry.getPosOnImage(event)
        if (smartAnnotationMode && mouseArea.interactionState === LabelCanvasEnums.Idle) {
            smartAnnotationController.handleHover(pos, true)
            return
        }
        if (polygonToolMode && polygonTool.drawingPolygon && mouseArea.interactionState === LabelCanvasEnums.Idle) {
            polygonTool.updatePreview(pos)
            return
        }

        if (selectToolMode && mouseArea.interactionState === LabelCanvasEnums.ReadyEdit) {
            beginLabelEditing(pos)
        } else if (mouseArea.interactionState === LabelCanvasEnums.ReadyDraw) {
            beginRectangleDrawing()
        }

        updateActiveMouseState(event, pos)
    }

    function handleMouseDoubleClicked(event) {
        if (classificationMode) {
            return
        }
        if (polygonTool.handleDoubleClicked(event)) {
            mouseArea.suppressNextRelease = true
            return
        }
        if (selectToolMode && segmentationMode && event.button === Qt.LeftButton) {
            let pos = canvasGeometry.getPosOnImage(event)
            if (polygonTool.insertPointOnSelectedPolygonEdge(pos)) {
                mouseArea.interactionState = LabelCanvasEnums.Idle
                mouseArea.suppressNextRelease = true
                event.accepted = true
            }
        }
    }

    function beginToolPress(pos) {
        startPos = pos
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

    function finishLabelEditing(event) {
        let item = labelsListView.itemAt(mouseArea.activeData.index)
        if (dataManager && mouseArea.activeData.label_id !== -1) {
            dataManager.updateLabels([mouseArea.activeData.label_id], [mouseArea.activeData])
        }
        let pos = canvasGeometry.getPosOnImage(event)
        if (!hitTest(pos)) {
            mouseArea.cursorShape = Qt.ArrowCursor
        }
        if (item) {
            item.visible = true
        }
        drawingItem.clearItem()
    }

    function finishReadyEdit(event) {
        let pos = canvasGeometry.getPosOnImage(event)
        let hit = hitTest(pos)
        if (hit && hit.found && (hit.edge_index !== undefined || hit.mode === 1)) {
            return
        }
        if (event.button === Qt.LeftButton) {
            setIdleCursor(event.modifiers)
            selectAt(pos)
        }
    }

    function handleIdleRelease(event) {
        setIdleCursor(event.modifiers)
        let pos = canvasGeometry.getPosOnImage(event)
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
        let selectedAtPos = false
        if (selection) {
            for (let index of indices) {
                if (selection.isSelected(imageLabelsList.index(index, 0))) {
                    selectedAtPos = true
                    break
                }
            }
        }
        if (!selectedAtPos) {
            let newIndex = imageLabelsList.chooseIndex(indices)
            select(newIndex, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
        }
        labelCanvasActions.popup()
    }

    function beginLabelEditing(pos) {
        mouseArea.interactionState = LabelCanvasEnums.Editing
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

    function updateActiveMouseState(event, pos) {
        if (mouseArea.interactionState === LabelCanvasEnums.Drawing) {
            updateRectangleDrawing(pos)
        } else if (mouseArea.interactionState === LabelCanvasEnums.Dragging) {
            moveImage(event)
        } else if (mouseArea.interactionState === LabelCanvasEnums.Editing) {
            updateLabelEditing(pos)
        } else if (selectToolMode) {
            updateSelectionHover(pos)
        } else if (imageLabelsList) {
            imageLabelsList.setHovered([])
        }
    }

    function updateRectangleDrawing(pos) {
        let rect = canvasGeometry.rectFromPoints(startPos, pos)
        drawingItem.updateItem(newDraftLabelData(rect))
    }

    function updateLabelEditing(pos) {
        mouseArea.activeData = imageLabelsList.getEditedData(mouseArea.activeData, startPos, pos)
        drawingItem.updateItem(mouseArea.activeData)
        startPos = pos
    }

    function updateSelectionHover(pos) {
        if (!imageLabelsList) {
            return
        }
        if (classificationMode) {
            return
        }
        if (!hitTest(pos)) {
            mouseArea.cursorShape = Qt.ArrowCursor
            imageLabelsList.setHovered(imageLabelsList.getIndicesAt(pos))
        }
    }

    function select(index, command) {
        if (selection) {
            selection.select(index, command)
            SignalHelper.imageLabelListSelectionChanged(index, command)
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

    function activateToolMode(mode) {
        if (mode === LabelCanvasEnums.PolygonTool && !segmentationMode) {
            return false
        }
        if (mode === LabelCanvasEnums.SmartTool && !smartAnnotationAvailable) {
            return false
        }

        setToolMode(mode)
        forceActiveFocus()
        return true
    }

    function moveImage(event) {
        let dx = event.x - startPos.x
        let dy = event.y - startPos.y
        labelImage.image.x += dx
        labelImage.image.y += dy
        startPos = Qt.point(event.x, event.y)
    }

    function resetDragState() {
        if (mouseArea.interactionState === LabelCanvasEnums.Dragging) {
            mouseArea.cursorShape = Qt.ArrowCursor
        }
        mouseArea.interactionState = LabelCanvasEnums.Idle
        mouseArea.suppressNextRelease = false
    }

    function hitTest(pos) {
        if (classificationMode || !selectToolMode || !imageLabelsList) {
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
        labelFillOpacity = Math.max(0, Math.min(1, GlobalSettings.valueForField(
                    SettingsAccessor.Data,
                    DataField.FillOpacity,
                    30) / 100.0))
    }

    function newDraftLabelData(fields) {
        let data = {label_id: -1, color: drawingColor}
        for (let key in fields) {
            data[key] = fields[key]
        }
        return data
    }

    Component.onCompleted: {
        refreshSettings()
        refreshClassificationBadge()
    }
}
