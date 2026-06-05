import QtQuick
import QtQuick.Controls

import dltool.ui
import dltool.data
import dltool.settings

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
    property string toolMode: "select"
    property bool showBoundingBoxes: false
    readonly property bool smartAnnotationAvailable: smartAnnotation
                                                    && GlobalSettings.data.smartAnnotationEnabled
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

    onToolModeChanged: {
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

    onSmartAnnotationAvailableChanged: {
        if (!smartAnnotationAvailable && toolMode === "smart") {
            toolMode = "select"
        }
        if (!smartAnnotationAvailable) {
            clearSmartAnnotation()
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
        interval: Math.max(20, GlobalSettings.data.smartAnnotationRefreshInterval)
        repeat: false
        onTriggered: updateSmartAnnotationPreview()
    }

    DltMenu {
        id: labelCanvasMenu
        width: 200
        DltMenuItem {
            text: "删除选中标签实例"
            enabled : selection ? selection.hasSelection : false
            iconSource: DltFontIcon.Delete
            onClicked: {
                deleteConfirmDialog.open()
            }
        }
    }

    DltContentDialog {
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
                 && GlobalSettings.data.smartAnnotationMaskAlpha > 0

        onPaint: {
            let ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!visible) {
                return
            }

            let scale = labelImage.image.scale
            let offsetX = labelImage.image.x
            let offsetY = labelImage.image.y
            let alpha = Math.max(0, Math.min(1, GlobalSettings.data.smartAnnotationMaskAlpha))
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

        onPaint: {
            let ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!visible) {
                return
            }

            ctx.save()
            for (let point of smartPoints) {
                let screen = toScreen(point)
                ctx.beginPath()
                ctx.arc(screen.x, screen.y, 6, 0, Math.PI * 2)
                ctx.fillStyle = point.label > 0 ? "#20B15A" : "#E5484D"
                ctx.fill()
                ctx.lineWidth = 2
                ctx.strokeStyle = "white"
                ctx.stroke()
            }
            if (smartHoverPointValid) {
                let screen = toScreen(smartHoverPoint)
                ctx.beginPath()
                ctx.arc(screen.x, screen.y, 6, 0, Math.PI * 2)
                ctx.fillStyle = "#20B15A"
                ctx.globalAlpha = 0.65
                ctx.fill()
                ctx.globalAlpha = 1
                ctx.lineWidth = 2
                ctx.strokeStyle = "white"
                ctx.stroke()
            }
            ctx.restore()
        }
    }

    Connections {
        target: labelImage.image
        function onXChanged() { smartMaskCanvas.requestPaint(); smartPromptCanvas.requestPaint() }
        function onYChanged() { smartMaskCanvas.requestPaint(); smartPromptCanvas.requestPaint() }
        function onScaleChanged() { smartMaskCanvas.requestPaint(); smartPromptCanvas.requestPaint() }
        function onStatusChanged() { smartMaskCanvas.requestPaint(); smartPromptCanvas.requestPaint() }
    }

    Connections {
        target: GlobalSettings.data
        function onSmartAnnotationMaskAlphaChanged() { smartMaskCanvas.requestPaint() }
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

    Keys.onPressed: function(event) {
        if (smartAnnotationMode && event.key === Qt.Key_Escape) {
            clearSmartAnnotation()
            event.accepted = true
        } else if (smartAnnotationMode && (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            confirmSmartAnnotation()
            event.accepted = true
        } else if (event.key === Qt.Key_Control && !mouseArea.pressed) {
            mouseArea.cursorShape = Qt.OpenHandCursor
        } else if (event.key === Qt.Key_A && event.modifiers & Qt.ControlModifier) {
            imageLabelsList.selectAll()
        } else if (event.key === Qt.Key_Delete && selection && selection.hasSelection) {
            deleteConfirmDialog.open()
        } else if (event.key === Qt.Key_Escape && polygonToolMode && mouseArea.drawingPolygon) {
            cancelPolygonDrawing()
            event.accepted = true
        } else if (labelClasses && event.text.length > 0) {
            // 快捷键切换标签类别
            if (labelClasses.selectByShortcut(event.text)) {
                event.accepted = true
            }
        }
    }

    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_Control  && !mouseArea.pressed) {
            mouseArea.cursorShape = Qt.ArrowCursor
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        property string state: "idle"
        property var data: null
        property bool drawingPolygon: false
        property bool suppressNextRelease: false

        onPressed: function(event) {
            if (state === "idle" && (event.button === Qt.MiddleButton || (event.modifiers & Qt.ControlModifier && event.button === Qt.LeftButton))) {
                mouseArea.cursorShape = Qt.ClosedHandCursor
                startPos = Qt.point(event.x, event.y)
                state = "dragging"
            } else if (smartAnnotationMode && state === "idle"
                       && (event.button === Qt.LeftButton || event.button === Qt.RightButton)) {
                appendSmartPoint(getPosOnImage(event), event.button === Qt.LeftButton ? 1 : 0)
                event.accepted = true
            } else if (polygonToolMode && state === "idle" && event.button === Qt.RightButton && drawingPolygon) {
                finishPolygonDrawing()
                suppressNextRelease = true
                event.accepted = true
            } else if (polygonToolMode && state === "idle" && event.button === Qt.LeftButton) {
                startPos = getPosOnImage(event)
                appendPolygonPoint(startPos)
                event.accepted = true
            } else if (state === "idle" && event.button === Qt.RightButton) {

            } else if (state === "idle" && event.button === Qt.LeftButton) {
                // 获取相对于LabelImage的坐标
                startPos = getPosOnImage(event)
                if (rectangleToolMode) {
                    state = "readyDraw"
                    clearSelection()
                } else if (selectToolMode) {
                    state = hitTest(startPos) ? "readyEdit" : "idle"
                } else {
                    state = "idle"
                }
                imageLabelsList.setHovered([])
            } 
        }

        onReleased: function(event) {
            if (suppressNextRelease) {
                suppressNextRelease = false
                state = "idle"
                return
            }
            if (smartAnnotationMode && state === "idle"
                    && (event.button === Qt.LeftButton || event.button === Qt.RightButton)) {
                return
            }
            if (polygonToolMode && drawingPolygon && state === "idle") {
                updatePolygonPreview(getPosOnImage(event))
                return
            }
            if (!selectToolMode && state !== "drawing" && state !== "dragging") {
                mouseArea.cursorShape = event.modifiers & Qt.ControlModifier ? Qt.OpenHandCursor : Qt.ArrowCursor
                state = "idle"
                return
            }
            if (state === "drawing") {
                data = drawingItem.getData()
                // 添加到ListModel
                if (data.width > 1 && data.height > 1) {
                    addCurrentLabel(data)
                }
                drawingItem.clearItem()
            } else if (state === "dragging") {
                mouseArea.cursorShape = event.modifiers & Qt.ControlModifier ? Qt.OpenHandCursor : Qt.ArrowCursor
                startPos = Qt.point(event.x, event.y)
            } else if (state === "editing") {
                let item = labelsListView.itemAt(data.index)
                // data = drawingItem.getData()
                // 编辑结束时更新标注
                if (dataManager && data.label_id !== -1) {
                    dataManager.updateLabels([data.label_id], [data])
                }
                let pos = getPosOnImage(event)
                if (!hitTest(pos)) {
                    mouseArea.cursorShape = Qt.ArrowCursor
                }
                if (item) {
                    item.visible = true
                }
                drawingItem.clearItem()
            } else if (state === "readyEdit") {
                let pos = getPosOnImage(event)
                let hit = hitTest(pos)
                if (isEditHandleHit(hit)) {
                    if (!hit) {
                        mouseArea.cursorShape = Qt.ArrowCursor
                    }
                } else if (event.button === Qt.LeftButton) {
                    mouseArea.cursorShape = event.modifiers & Qt.ControlModifier ? Qt.OpenHandCursor : Qt.ArrowCursor
                    let indices = imageLabelsList.getIndicesAt(pos)
                    let new_index = imageLabelsList.chooseIndex(indices)
                    select(new_index, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                    hitTest(pos)
                }
            } else {
                mouseArea.cursorShape = event.modifiers & Qt.ControlModifier ? Qt.OpenHandCursor : Qt.ArrowCursor
                if (event.button === Qt.LeftButton) {
                    let pos = getPosOnImage(event)
                    let indices = imageLabelsList.getIndicesAt(pos)
                    let new_index = imageLabelsList.chooseIndex(indices)
                    select(new_index, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                    hitTest(pos)
                } else if (event.button === Qt.RightButton) { // 右键在有选中的情况下不选中新的
                    let pos = getPosOnImage(event)
                    let indices = imageLabelsList.getIndicesAt(pos)                    
                    let hasSelected = false
                    for (let index of indices) {
                        hasSelected |= selection.isSelected(imageLabelsList.index(index, 0))
                    }
                    if (!hasSelected) {
                        let new_index = imageLabelsList.chooseIndex(indices)
                        select(new_index, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                    }
                    labelCanvasMenu.popup()
                }
            }
            state = "idle"
        }

        onPositionChanged: function(event) {
            if (smartAnnotationMode && state === "idle") {
                setSmartHoverPoint(getPosOnImage(event), true)
                return
            }
            if (polygonToolMode && drawingPolygon && state === "idle") {
                updatePolygonPreview(getPosOnImage(event))
                return
            }
            if (selectToolMode && state === "readyEdit") {
                state = "editing"
                let pos = getPosOnImage(event)
                let hit = hitTest(pos)
                let index = imageLabelsList.getTopSelectedIndex()
                let item = labelsListView.itemAt(index)
                if (item) {
                    item.visible = false
                }
                data = imageLabelsList.getData(index)
                data.hit = hit
                drawingItem.initItem(data)
            } else if (state === "readyDraw") {
                state = "drawing"
                drawingItem.initItem({label_id: -1, x: startPos.x, y: startPos.y, width: 0, height: 0, color: drawingColor})
            }
            if (state === "drawing") {
                let endPos = getPosOnImage(event)
                let rect = getRect(startPos, endPos)
                drawingItem.updateItem({label_id: -1, x: rect.x, y: rect.y, width: rect.width, height: rect.height, color: drawingColor})
            }  else if (state === "dragging") {
                moveImage(event)
            } else if (state === "editing") {
                let endPos = getPosOnImage(event)
                data = imageLabelsList.getEditedData(data, startPos, endPos)
                drawingItem.updateItem(data)
                startPos = endPos
            } else if (selectToolMode) {
                let pos = getPosOnImage(event)
                if (!hitTest(pos)) {
                    mouseArea.cursorShape = Qt.ArrowCursor
                    let indices = imageLabelsList.getIndicesAt(pos)
                    imageLabelsList.setHovered(indices)
                }
            } else {
                imageLabelsList.setHovered([])
            }
        }

        onEntered: {
            labelView.forceActiveFocus()
        }

        onExited: {
            if (smartAnnotationMode && smartHoverPointValid) {
                clearSmartHoverPoint()
                smartAnnotationDirty = true
                smartPreviewTimer.restart()
            }
        }

        onDoubleClicked: function(event) {
            if (polygonToolMode && event.button === Qt.LeftButton && drawingPolygon) {
                finishPolygonDrawing()
                suppressNextRelease = true
                event.accepted = true
                return
            }
            if (selectToolMode && segmentationMode && event.button === Qt.LeftButton) {
                let pos = getPosOnImage(event)
                if (insertPointOnSelectedPolygonEdge(pos)) {
                    state = "idle"
                    suppressNextRelease = true
                    event.accepted = true
                }
            }
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
        if (!smartAnnotation || !imageInstances || prompts.length === 0) {
            smartAnnotationResult = ({})
            smartAnnotationDirty = false
            drawingItem.clearItem()
            return
        }

        let result = smartAnnotation.infer(imageInstances.currentImagePath, prompts)
        if (!result || result.success !== true) {
            smartAnnotationResult = ({})
            smartAnnotationDirty = false
            drawingItem.clearItem()
            return
        }

        smartAnnotationResult = result
        smartAnnotationDirty = false
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
        if (!dataManager || !imageInstances || !labelClasses || labelClasses.currentLabelClassId === -1) {
            return
        }

        if (!smartHoverPointValid && smartPoints.length === 0 && mouseArea.containsMouse) {
            setSmartHoverPoint(getPosOnImagePoint(mouseArea.mouseX, mouseArea.mouseY), false)
        }
        if (smartPromptPoints().length === 0) {
            return
        }

        if (smartAnnotationDirty || !smartAnnotationResult || smartAnnotationResult.success !== true) {
            updateSmartAnnotationPreview()
        }
        if (!smartAnnotationResult || smartAnnotationResult.success !== true) {
            return
        }

        let data = {}
        if (segmentationMode && smartAnnotationResult.points && smartAnnotationResult.points.length >= 3) {
            data.points = clonePoints(smartAnnotationResult.points)
            data.x = smartAnnotationResult.x
            data.y = smartAnnotationResult.y
            data.width = smartAnnotationResult.width
            data.height = smartAnnotationResult.height
        } else if (smartAnnotationResult.width > 1 && smartAnnotationResult.height > 1) {
            data.x = smartAnnotationResult.x
            data.y = smartAnnotationResult.y
            data.width = smartAnnotationResult.width
            data.height = smartAnnotationResult.height
        } else {
            return
        }

        if (addCurrentLabel(data)) {
            clearSmartAnnotation()
        }
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
}
