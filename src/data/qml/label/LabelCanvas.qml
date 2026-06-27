import QtQuick
import QtQuick.Controls

import dltool.ui
import dltool.core
import dltool.data
import dltool.feature
import dltool.settings
import quickui

import "../component"
import "../gallery"

Item {
    id: labelView
    clip: true

    property DataManager dataManager
    property ImageInstancesModel imageInstances: dataManager ? dataManager.imageInstances : null
    property LabelClassesModel labelClasses: dataManager ? dataManager.labelClasses : null
    property ImageLabelsListModel imageLabelsList: dataManager ? dataManager.imageLabelsList : null
    property ItemSelectionModel selection: imageLabelsList ? imageLabelsList.selection : null
    property color drawingColor: labelClasses ? labelClasses.currentLabelClassColor : "red"
    property point startPos: Qt.point(0, 0)
    property bool segmentationMode: dataManager ? dataManager.method === DeepLearningMethod.Segmentation : false
    property var smartAnnotation: dataManager ? dataManager.smartAnnotation : null
    property bool roiSearchEnabled: true
    property int smartAnnotationRefreshInterval: 80
    property real smartAnnotationMaskAlpha: 0.35
    property string toolMode: "select"
    property bool showBoundingBoxes: false
    readonly property bool smartAnnotationAvailable: smartAnnotation
                                                    && smartAnnotation.enabled
                                                    && dataManager
                                                    && (dataManager.method === DeepLearningMethod.Detection
                                                        || dataManager.method === DeepLearningMethod.Segmentation)
    property bool smartAnnotationMode: toolMode === "smart" && smartAnnotationAvailable
    property bool selectToolMode: toolMode === "select"
    property bool rectangleToolMode: toolMode === "rect"
    property bool polygonToolMode: toolMode === "polygon" && segmentationMode
    property var smartPoints: []
    property var smartHoverPoint: ({})
    property bool smartHoverPointValid: false
    property var smartAnnotationResult: ({})
    property bool smartAnnotationDirty: false
    property var polygonPoints: []
    
    // 暴露图像缩放属性
    property real imageScale: labelImage.image.scale

    onToolModeChanged: handleToolModeChanged()

    onSmartAnnotationAvailableChanged: handleSmartAnnotationAvailableChanged()

    Connections {
        target: SignalHelper
        function onImageLabelTableSelectionChanged(index, command) {
            if (selection) {
                selection.select(index, command)
            }
        }
        function onImageLabelTableShiftSelect(currentIndex, lastIndex, command) {
            imageLabelsList.shiftSelect(currentIndex, lastIndex, command)
        }
        function onImageLabelTableSelectAll() {
            imageLabelsList.selectAll()
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
            cancelPolygonDrawing()
            clearSmartAnnotation()
        }
    }

    Timer {
        id: smartPreviewTimer
        interval: Math.max(20, labelView.smartAnnotationRefreshInterval)
        repeat: false
        onTriggered: updateSmartAnnotationPreview()
    }

    QuiMenu {
        id: labelCanvasMenu
        width: 200
        QuiMenuItem {
            text: "图像搜索"
            enabled: dataManager && dataManager.imageSearch
                     && imageInstances && imageInstances.currentImageId >= 0
                     && !dataManager.imageSearch.running
                     && dataManager.imageSearch.enabled
iconSource: QuiFontIcon.Search
            onClicked: startImageSearchForCurrentImage()
        }
        QuiMenuItem {
            text: "标注搜索"
            enabled: dataManager && dataManager.imageSearch
                     && selection && selection.hasSelection
                     && !dataManager.imageSearch.running
                     && roiSearchEnabled
            iconSource: QuiFontIcon.Search
            onClicked: startRoiSearchForSelectedLabels()
        }
        QuiMenuItem {
            text: "删除选中标签实例"
            enabled : selection ? selection.hasSelection : false
            iconSource: QuiFontIcon.Delete
            onClicked: {
                deleteConfirmDialog.open()
            }
        }
    }

    QuiContentDialog {
        id: deleteConfirmDialog
        title: "删除选中标签实例"
        message: "确定删除选中的标签实例吗?"
        onPositiveClicked: function () {
            if (dataManager) {
                let label_ids = imageLabelsList.getSelectedLabelIds()
                dataManager.deleteLabels(label_ids)
            }
        }
    }

    ImageSearchDialog {
        id: imageSearchDialog
        dataManager: labelView.dataManager
    }

    RoiSearchDialog {
        id: roiSearchDialog
        dataManager: labelView.dataManager
    }

    LabelImage {
        id: labelImage
        anchors.fill: parent
        currentImagePath: imageInstances ? imageInstances.currentImagePath : ""
    }

    Canvas {
        id: smartMaskCanvas
        anchors.fill: parent
        antialiasing: false
        visible: smartAnnotationMode
                 && smartAnnotationResult
                 && smartAnnotationResult.success === true
                 && smartAnnotationResult.mask_runs
                 && smartAnnotationResult.mask_runs.length > 0
                 && smartAnnotationMaskAlpha > 0

        onPaint: paintSmartMask()
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

    Canvas {
        id: smartPromptCanvas
        anchors.fill: parent
        antialiasing: true
        visible: smartAnnotationMode && (smartPoints.length > 0 || smartHoverPointValid)

        onPaint: paintSmartPrompts()
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

    onDrawingColorChanged: smartMaskCanvas.requestPaint()
    onSmartAnnotationResultChanged: smartMaskCanvas.requestPaint()

    CrosshairCanvas {
        visible: mouseArea.containsMouse
                 && labelImage.image.status === Image.Ready
                 && mouseArea.state !== "dragging"
                 && mouseArea.state !== "editing"
        mousePos: Qt.point(mouseArea.mouseX, mouseArea.mouseY)
    }

    Keys.onPressed: function(event) { handleKeyPressed(event) }

    Keys.onReleased: function(event) { handleKeyReleased(event) }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        property string state: "idle"
        property var data: null
        property bool drawingPolygon: false
        property bool suppressNextRelease: false

        onPressed: function(event) { handleMousePressed(event) }

        onReleased: function(event) { handleMouseReleased(event) }

        onPositionChanged: function(event) { handleMousePositionChanged(event) }

        onEntered: {
            labelView.forceActiveFocus()
        }

        onExited: handleMouseExited()

        onDoubleClicked: function(event) { handleMouseDoubleClicked(event) }
    }

    function handleToolModeChanged() {
        mouseArea.state = "idle"
        mouseArea.cursorShape = Qt.ArrowCursor
        if (toolMode !== "polygon") {
            cancelPolygonDrawing()
        }
        if (toolMode !== "smart") {
            clearSmartAnnotation()
        } else if (mouseArea.containsMouse) {
            setSmartHoverPoint(getPosOnImagePoint(mouseArea.mouseX, mouseArea.mouseY), true)
        }
    }

    function handleSmartAnnotationAvailableChanged() {
        if (!smartAnnotationAvailable && toolMode === "smart") {
            toolMode = "select"
        }
        if (!smartAnnotationAvailable) {
            clearSmartAnnotation()
        }
    }

    function requestSmartOverlayPaint() {
        smartMaskCanvas.requestPaint()
        smartPromptCanvas.requestPaint()
    }

    function paintSmartMask() {
        let ctx = smartMaskCanvas.getContext("2d")
        ctx.clearRect(0, 0, smartMaskCanvas.width, smartMaskCanvas.height)
        if (!smartMaskCanvas.visible) {
            return
        }

        let scale = labelImage.image.scale
        let offsetX = labelImage.image.x
        let offsetY = labelImage.image.y
        let alpha = Math.max(0, Math.min(1, smartAnnotationMaskAlpha))
        ctx.save()
        ctx.fillStyle = Qt.rgba(drawingColor.r, drawingColor.g, drawingColor.b, alpha)
        for (let run of smartAnnotationResult.mask_runs) {
            ctx.fillRect(offsetX + run.x * scale,
                         offsetY + run.y * scale,
                         Math.max(1, run.width * scale),
                         Math.max(1, scale))
        }
        ctx.restore()
    }

    function paintSmartPrompts() {
        let ctx = smartPromptCanvas.getContext("2d")
        ctx.clearRect(0, 0, smartPromptCanvas.width, smartPromptCanvas.height)
        if (!smartPromptCanvas.visible) {
            return
        }

        ctx.save()
        for (let point of smartPoints) {
            paintSmartPromptPoint(ctx, point, point.label > 0 ? "#20B15A" : "#E5484D", 1)
        }
        if (smartHoverPointValid) {
            paintSmartPromptPoint(ctx, smartHoverPoint, "#20B15A", 0.65)
        }
        ctx.restore()
    }

    function paintSmartPromptPoint(ctx, point, fillColor, alpha) {
        let screen = toScreen(point)
        ctx.beginPath()
        ctx.arc(screen.x, screen.y, 6, 0, Math.PI * 2)
        ctx.fillStyle = fillColor
        ctx.globalAlpha = alpha
        ctx.fill()
        ctx.globalAlpha = 1
        ctx.lineWidth = 2
        ctx.strokeStyle = "white"
        ctx.stroke()
    }

    function handleKeyPressed(event) {
        if (smartAnnotationMode && event.key === Qt.Key_Escape) {
            clearSmartAnnotation()
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
            imageLabelsList.selectAll()
            SignalHelper.imageLabelListSelectAll()
            return
        }
        if (event.key === Qt.Key_Delete && selection && selection.hasSelection) {
            deleteConfirmDialog.open()
            return
        }
        if (event.key === Qt.Key_Escape && polygonToolMode && mouseArea.drawingPolygon) {
            cancelPolygonDrawing()
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
        if (!labelClasses || event.text.length <= 0) {
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
        if (handleSmartAnnotationPress(event)) {
            return
        }
        if (handlePolygonPress(event)) {
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
            updatePolygonPreview(getPosOnImage(event))
            return
        }
        if (isIgnoredNonSelectRelease()) {
            setIdleCursor(event.modifiers)
            mouseArea.state = "idle"
            return
        }

        if (mouseArea.state === "drawing") {
            finishRectangleDrawing()
        } else if (mouseArea.state === "dragging") {
            finishImageDrag(event)
        } else if (mouseArea.state === "editing") {
            finishLabelEditing(event)
        } else if (mouseArea.state === "readyEdit") {
            finishReadyEdit(event)
        } else {
            handleIdleRelease(event)
        }
        mouseArea.state = "idle"
    }

    function handleMousePositionChanged(event) {
        if (smartAnnotationMode && isIdle()) {
            setSmartHoverPoint(getPosOnImage(event), true)
            return
        }
        if (polygonToolMode && mouseArea.drawingPolygon && isIdle()) {
            updatePolygonPreview(getPosOnImage(event))
            return
        }

        if (selectToolMode && mouseArea.state === "readyEdit") {
            beginLabelEditing(event)
        } else if (mouseArea.state === "readyDraw") {
            beginRectangleDrawing()
        }

        updateActiveMouseState(event)
    }

    function handleMouseExited() {
        if (smartAnnotationMode && smartHoverPointValid) {
            clearSmartHoverPoint()
            smartAnnotationDirty = true
            smartPreviewTimer.restart()
        }
    }

    function handleMouseDoubleClicked(event) {
        if (polygonToolMode && event.button === Qt.LeftButton && mouseArea.drawingPolygon) {
            finishPolygonDrawing()
            mouseArea.suppressNextRelease = true
            event.accepted = true
            return
        }
        if (selectToolMode && segmentationMode && event.button === Qt.LeftButton) {
            let pos = getPosOnImage(event)
            if (insertPointOnSelectedPolygonEdge(pos)) {
                mouseArea.state = "idle"
                mouseArea.suppressNextRelease = true
                event.accepted = true
            }
        }
    }

    function isIdle() {
        return mouseArea.state === "idle"
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
        mouseArea.state = "dragging"
    }

    function handleSmartAnnotationPress(event) {
        if (!smartAnnotationMode || !isPrimaryOrSecondaryButton(event)) {
            return false
        }
        appendSmartPoint(getPosOnImage(event), event.button === Qt.LeftButton ? 1 : 0)
        event.accepted = true
        return true
    }

    function handlePolygonPress(event) {
        if (!polygonToolMode) {
            return false
        }
        if (event.button === Qt.RightButton && mouseArea.drawingPolygon) {
            finishPolygonDrawing()
            mouseArea.suppressNextRelease = true
            event.accepted = true
            return true
        }
        if (event.button === Qt.LeftButton) {
            startPos = getPosOnImage(event)
            appendPolygonPoint(startPos)
            event.accepted = true
            return true
        }
        return false
    }

    function beginToolPress(event) {
        startPos = getPosOnImage(event)
        if (rectangleToolMode) {
            mouseArea.state = "readyDraw"
            clearSelection()
        } else if (selectToolMode) {
            mouseArea.state = hitTest(startPos) ? "readyEdit" : "idle"
        } else {
            mouseArea.state = "idle"
        }
        imageLabelsList.setHovered([])
    }

    function consumeSuppressedRelease() {
        if (!mouseArea.suppressNextRelease) {
            return false
        }
        mouseArea.suppressNextRelease = false
        mouseArea.state = "idle"
        return true
    }

    function isSmartAnnotationPointRelease(event) {
        return smartAnnotationMode && isIdle() && isPrimaryOrSecondaryButton(event)
    }

    function isPolygonPreviewRelease() {
        return polygonToolMode && mouseArea.drawingPolygon && isIdle()
    }

    function isIgnoredNonSelectRelease() {
        return !selectToolMode && mouseArea.state !== "drawing" && mouseArea.state !== "dragging"
    }

    function setIdleCursor(modifiers) {
        mouseArea.cursorShape = (modifiers & Qt.ControlModifier) ? Qt.OpenHandCursor : Qt.ArrowCursor
    }

    function finishRectangleDrawing() {
        mouseArea.data = drawingItem.getData()
        if (mouseArea.data.width > 1 && mouseArea.data.height > 1) {
            addCurrentLabel(mouseArea.data)
        }
        drawingItem.clearItem()
    }

    function finishImageDrag(event) {
        setIdleCursor(event.modifiers)
        startPos = Qt.point(event.x, event.y)
    }

    function finishLabelEditing(event) {
        let item = labelsListView.itemAt(mouseArea.data.index)
        if (dataManager && mouseArea.data.label_id !== -1) {
            dataManager.updateLabels([mouseArea.data.label_id], [mouseArea.data])
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
        let indices = imageLabelsList.getIndicesAt(pos)
        let newIndex = imageLabelsList.chooseIndex(indices)
        select(newIndex, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
        hitTest(pos)
    }

    function openContextMenuAt(pos) {
        let indices = imageLabelsList.getIndicesAt(pos)
        if (!hasSelectedIndex(indices)) {
            let newIndex = imageLabelsList.chooseIndex(indices)
            select(newIndex, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
        }
        labelCanvasMenu.popup()
    }

    function hasSelectedIndex(indices) {
        if (!selection) {
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
        mouseArea.state = "editing"
        let pos = getPosOnImage(event)
        let hit = hitTest(pos)
        let index = imageLabelsList.getTopSelectedIndex()
        let item = labelsListView.itemAt(index)
        if (item) {
            item.visible = false
        }
        mouseArea.data = imageLabelsList.getData(index)
        mouseArea.data.hit = hit
        drawingItem.initItem(mouseArea.data)
    }

    function beginRectangleDrawing() {
        mouseArea.state = "drawing"
        drawingItem.initItem({label_id: -1, x: startPos.x, y: startPos.y, width: 0, height: 0, color: drawingColor})
    }

    function updateActiveMouseState(event) {
        if (mouseArea.state === "drawing") {
            updateRectangleDrawing(event)
        } else if (mouseArea.state === "dragging") {
            moveImage(event)
        } else if (mouseArea.state === "editing") {
            updateLabelEditing(event)
        } else if (selectToolMode) {
            updateSelectionHover(event)
        } else {
            imageLabelsList.setHovered([])
        }
    }

    function updateRectangleDrawing(event) {
        let endPos = getPosOnImage(event)
        let rect = getRect(startPos, endPos)
        drawingItem.updateItem({label_id: -1, x: rect.x, y: rect.y, width: rect.width, height: rect.height, color: drawingColor})
    }

    function updateLabelEditing(event) {
        let endPos = getPosOnImage(event)
        mouseArea.data = imageLabelsList.getEditedData(mouseArea.data, startPos, endPos)
        drawingItem.updateItem(mouseArea.data)
        startPos = endPos
    }

    function updateSelectionHover(event) {
        let pos = getPosOnImage(event)
        if (!hitTest(pos)) {
            mouseArea.cursorShape = Qt.ArrowCursor
            let indices = imageLabelsList.getIndicesAt(pos)
            imageLabelsList.setHovered(indices)
        }
    }

    function clonePoints(points) {
        let result = []
        if (!points) {
            return result
        }
        for (let point of points) {
            result.push({x: point.x, y: point.y})
        }
        return result
    }

    function clampPointToImage(point) {
        if (labelImage.image.status !== Image.Ready) {
            return point
        }
        return Qt.point(Math.max(0, Math.min(labelImage.image.sourceSize.width, point.x)),
                        Math.max(0, Math.min(labelImage.image.sourceSize.height, point.y)))
    }

    function distance(pt1, pt2) {
        let dx = pt1.x - pt2.x
        let dy = pt1.y - pt2.y
        return Math.sqrt(dx * dx + dy * dy)
    }

    function getPolygonBounds(points) {
        if (!points || points.length === 0) {
            return {x: 0, y: 0, width: 0, height: 0}
        }

        let xMin = points[0].x
        let yMin = points[0].y
        let xMax = points[0].x
        let yMax = points[0].y
        for (let point of points) {
            xMin = Math.min(xMin, point.x)
            yMin = Math.min(yMin, point.y)
            xMax = Math.max(xMax, point.x)
            yMax = Math.max(yMax, point.y)
        }
        return {x: xMin, y: yMin, width: xMax - xMin, height: yMax - yMin}
    }

    function insertPointOnSelectedPolygonEdge(pos) {
        if (!dataManager || !imageLabelsList || !selection || !selection.hasSelection) {
            return false
        }

        let selectedIndex = imageLabelsList.getTopSelectedIndex()
        if (selectedIndex === -1) {
            return false
        }

        let hit = imageLabelsList.hitTestHandle(pos, selectedIndex, labelImage.image.scale)
        if (!hit.found || hit.edge_index === undefined) {
            return false
        }

        let data = imageLabelsList.getData(selectedIndex)
        if (!data || data.label_id === undefined || data.label_id === -1
                || !data.points || data.points.length < 3) {
            return false
        }

        let points = clonePoints(data.points)
        let edgeIndex = Number(hit.edge_index)
        if (isNaN(edgeIndex) || Math.floor(edgeIndex) !== edgeIndex
                || edgeIndex < 0 || edgeIndex >= points.length) {
            return false
        }

        let imagePos = clampPointToImage(pos)
        points.splice(edgeIndex + 1, 0, {x: imagePos.x, y: imagePos.y})

        let bounds = getPolygonBounds(points)
        data.x = bounds.x
        data.y = bounds.y
        data.width = bounds.width
        data.height = bounds.height
        data.point_count = points.length
        data.points = points
        delete data.hit

        dataManager.updateLabels([data.label_id], [data])
        return true
    }

    function isEditHandleHit(hit) {
        return hit && hit.found && (hit.edge_index !== undefined || hit.mode === 1)
    }

    function cloneSmartPoints(points) {
        let result = []
        if (!points) {
            return result
        }
        for (let point of points) {
            result.push({x: point.x, y: point.y, label: point.label})
        }
        return result
    }

    function toScreen(point) {
        return Qt.point(labelImage.image.x + point.x * labelImage.image.scale,
                        labelImage.image.y + point.y * labelImage.image.scale)
    }

    function isPointInsideImage(point) {
        if (labelImage.image.status !== Image.Ready) {
            return false
        }
        return point.x >= 0 && point.y >= 0
               && point.x <= labelImage.image.sourceSize.width
               && point.y <= labelImage.image.sourceSize.height
    }

    function smartPromptPoints() {
        let points = cloneSmartPoints(smartPoints)
        if (smartHoverPointValid) {
            points.push({x: smartHoverPoint.x, y: smartHoverPoint.y, label: 1})
        }
        return points
    }

    function clearSmartHoverPoint() {
        smartHoverPoint = ({})
        smartHoverPointValid = false
        smartPromptCanvas.requestPaint()
    }

    function setSmartHoverPoint(pos, schedulePreview) {
        if (!smartAnnotationMode) {
            return
        }
        if (!isPointInsideImage(pos)) {
            if (smartHoverPointValid) {
                clearSmartHoverPoint()
                smartAnnotationDirty = true
                if (schedulePreview) {
                    smartPreviewTimer.restart()
                }
            }
            return
        }

        let imagePos = clampPointToImage(pos)
        if (smartHoverPointValid && distance(imagePos, smartHoverPoint) < 0.5) {
            return
        }

        smartHoverPoint = {x: imagePos.x, y: imagePos.y, label: 1}
        smartHoverPointValid = true
        smartAnnotationDirty = true
        smartPromptCanvas.requestPaint()
        if (schedulePreview) {
            smartPreviewTimer.restart()
        }
    }

    function appendSmartPoint(pos, label) {
        if (!smartAnnotationMode || !isPointInsideImage(pos)) {
            return
        }

        let imagePos = clampPointToImage(pos)
        let points = cloneSmartPoints(smartPoints)
        points.push({x: imagePos.x, y: imagePos.y, label: label})
        smartPoints = points
        clearSmartHoverPoint()
        smartAnnotationDirty = true
        updateSmartAnnotationPreview()
    }

    function updateSmartAnnotationPreview() {
        smartPreviewTimer.stop()
        let prompts = smartPromptPoints()
        if (!canRunSmartAnnotation(prompts)) {
            clearSmartPreview()
            return
        }

        let result = smartAnnotation.infer(imageInstances.currentImagePath, prompts)
        if (!isValidSmartAnnotationResult(result)) {
            clearSmartPreview()
            return
        }

        applySmartPreview(result)
    }

    function canRunSmartAnnotation(prompts) {
        return smartAnnotation && imageInstances && prompts.length > 0
    }

    function isValidSmartAnnotationResult(result) {
        return result && result.success === true
    }

    function clearSmartPreview() {
        smartAnnotationResult = ({})
        smartAnnotationDirty = false
        drawingItem.clearItem()
    }

    function applySmartPreview(result) {
        smartAnnotationResult = result
        smartAnnotationDirty = false
        updateSmartDrawingPreview(result)
    }

    function updateSmartDrawingPreview(result) {
        if (segmentationMode && result.points && result.points.length >= 3) {
            drawingItem.updateItem({label_id: -1, points: result.points, color: drawingColor})
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

    function confirmSmartAnnotation() {
        if (!canAddSmartAnnotation()) {
            return
        }

        ensureSmartPromptFromMouse()
        if (smartPromptPoints().length === 0) {
            return
        }

        if (!ensureSmartAnnotationResult()) {
            return
        }

        let data = buildSmartAnnotationLabelData(smartAnnotationResult)
        if (!data) {
            return
        }

        if (addCurrentLabel(data)) {
            clearSmartAnnotation()
        }
    }

    function canAddSmartAnnotation() {
        return dataManager && imageInstances && labelClasses && labelClasses.currentLabelClassId !== -1
    }

    function ensureSmartPromptFromMouse() {
        if (!smartHoverPointValid && smartPoints.length === 0 && mouseArea.containsMouse) {
            setSmartHoverPoint(getPosOnImagePoint(mouseArea.mouseX, mouseArea.mouseY), false)
        }
    }

    function ensureSmartAnnotationResult() {
        if (smartAnnotationDirty || !isValidSmartAnnotationResult(smartAnnotationResult)) {
            updateSmartAnnotationPreview()
        }
        return isValidSmartAnnotationResult(smartAnnotationResult)
    }

    function buildSmartAnnotationLabelData(result) {
        if (segmentationMode && result.points && result.points.length >= 3) {
            return {
                points: clonePoints(result.points),
                x: result.x,
                y: result.y,
                width: result.width,
                height: result.height
            }
        }
        if (result.width > 1 && result.height > 1) {
            return {
                x: result.x,
                y: result.y,
                width: result.width,
                height: result.height
            }
        }
        return null
    }

    function clearSmartAnnotation() {
        smartPreviewTimer.stop()
        smartPoints = []
        smartHoverPoint = ({})
        smartHoverPointValid = false
        smartAnnotationResult = ({})
        smartAnnotationDirty = false
        smartPromptCanvas.requestPaint()
        smartMaskCanvas.requestPaint()
        if (!mouseArea.drawingPolygon) {
            drawingItem.clearItem()
        }
    }

    function appendPolygonPoint(pos) {
        let imagePos = clampPointToImage(pos)
        if (mouseArea.drawingPolygon && polygonPoints.length >= 3) {
            let closeDistance = 10 / Math.max(labelImage.image.scale, 0.01)
            if (distance(imagePos, polygonPoints[0]) <= closeDistance) {
                finishPolygonDrawing()
                return
            }
        }

        if (!mouseArea.drawingPolygon) {
            clearSelection()
            polygonPoints = []
            mouseArea.drawingPolygon = true
        }

        let points = clonePoints(polygonPoints)
        points.push({x: imagePos.x, y: imagePos.y})
        polygonPoints = points
        updatePolygonPreview(imagePos)
    }

    function updatePolygonPreview(pos) {
        if (!mouseArea.drawingPolygon) {
            return
        }

        let points = clonePoints(polygonPoints)
        if (pos && points.length > 0) {
            let imagePos = clampPointToImage(pos)
            if (distance(imagePos, points[points.length - 1]) > 0.0001) {
                points.push({x: imagePos.x, y: imagePos.y})
            }
        }
        drawingItem.updateItem({label_id: -1, points: points, color: drawingColor})
    }

    function finishPolygonDrawing() {
        let points = clonePoints(polygonPoints)
        let bounds = getPolygonBounds(points)
        if (dataManager && imageInstances && labelClasses && labelClasses.currentLabelClassId !== -1
                && points.length >= 3 && bounds.width > 1 && bounds.height > 1) {
            addCurrentLabel({
                                points: points,
                                x: bounds.x,
                                y: bounds.y,
                                width: bounds.width,
                                height: bounds.height
                            })
        }
        cancelPolygonDrawing()
    }

    function cancelPolygonDrawing() {
        polygonPoints = []
        mouseArea.drawingPolygon = false
        drawingItem.clearItem()
    }

    function getRect(pt1, pt2) {
        // 计算矩形的位置和大小
        let x = Math.min(pt1.x, pt2.x)
        let y = Math.min(pt1.y, pt2.y)
        let width = Math.abs(pt2.x - pt1.x)
        let height = Math.abs(pt2.y - pt1.y)
        return { x: x, y: y, width: width, height: height }
    }

    function select(index, command) {
        if (selection) {
            selection.select(index, command)
            SignalHelper.imageLabelListSelectionChanged(index, command)
        }
    }

    function shiftSelect(currentIndex, lastIndex, command) {
        if (selection) {
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
        if (mode === "smart" && !smartAnnotationAvailable) {
            mode = "select"
        }
        if (mode === "polygon" && !segmentationMode) {
            mode = "select"
        }
        toolMode = mode
    }

    function deleteSelectedLabels() {
        if (selection && selection.hasSelection) {
            deleteConfirmDialog.open()
        }
    }

    function copySelectedLabels() {
        if (dataManager && selection && selection.hasSelection) {
            dataManager.duplicateSelectedLabels()
        }
    }

    function startImageSearchForCurrentImage() {
        if (!dataManager || !dataManager.imageSearch || !imageInstances
                || imageInstances.currentImageId < 0
                || !dataManager.imageSearch.enabled) {
            return
        }

        imageSearchDialog.openForImages([imageInstances.currentImageId])
    }

    function startRoiSearchForSelectedLabels() {
        if (!dataManager || !dataManager.imageSearch || !imageLabelsList
                || !selection || !selection.hasSelection
                || !roiSearchEnabled) {
            return
        }

        let labelIds = imageLabelsList.getSelectedLabelIds()
        if (labelIds.length > 0) {
            roiSearchDialog.openForLabels(labelIds)
        }
    }

    function moveImage(event) {
        let dx = event.x - startPos.x
        let dy = event.y - startPos.y
        labelImage.image.x += dx
        labelImage.image.y += dy
        startPos = Qt.point(event.x, event.y)
    }

    function getPosOnImage(event) {
        return getPosOnImagePoint(event.x, event.y)
    }

    function getPosOnImagePoint(x, y) {
        return Qt.point((x - labelImage.image.x) / labelImage.image.scale, (y - labelImage.image.y) / labelImage.image.scale)
    }

    function hitTest(pos) {
        if (!selectToolMode) {
            return null
        }
        if (selection === null || !selection.hasSelection) {
            return null
        }
        // 检查是否点击在已选中的标签上（优先角、边，其次内部）
        // 只支持单选编辑
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
    
    // 设置图像缩放
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

    Component.onCompleted: refreshSettings()
}
