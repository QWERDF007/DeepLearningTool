import QtQuick
import QtQuick.Controls

import dltool.ui
import dltool.data

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
    property var polygonPoints: []
    
    // 暴露图像缩放属性
    property real imageScale: labelImage.image.scale

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
        }
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

    LabelsListView {
        id: labelsListView
        offsetX: labelImage.image.x
        offsetY: labelImage.image.y
        factor: labelImage.image.scale
        model: labelImage.image.status === Image.Ready ? imageLabelsList : null
    }

    DrawingItem {
        id: drawingItem
        offsetX: labelImage.image.x
        offsetY: labelImage.image.y
        factor: labelImage.image.scale
    }

    CrosshairCanvas {
        visible: mouseArea.containsMouse && labelImage.image.status === Image.Ready
        mousePos: Qt.point(mouseArea.mouseX, mouseArea.mouseY)
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Control && !mouseArea.pressed) {
            mouseArea.cursorShape = Qt.OpenHandCursor
        } else if (event.key === Qt.Key_A && event.modifiers & Qt.ControlModifier) {
            imageLabelsList.selectAll()
        } else if (event.key === Qt.Key_Delete && selection && selection.hasSelection) {
            deleteConfirmDialog.open()
        } else if (event.key === Qt.Key_Escape && mouseArea.drawingPolygon) {
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
            } else if (segmentationMode && state === "idle" && event.button === Qt.RightButton && drawingPolygon) {
                finishPolygonDrawing()
                suppressNextRelease = true
                event.accepted = true
            } else if (segmentationMode && state === "idle" && event.button === Qt.LeftButton) {
                startPos = getPosOnImage(event)
                let hit = hitTest(startPos)
                if (hit) {
                    state = "readyEdit"
                    imageLabelsList.setHovered([])
                } else if (!drawingPolygon && imageLabelsList.getIndicesAt(startPos).length > 0) {
                    imageLabelsList.setHovered(imageLabelsList.getIndicesAt(startPos))
                } else {
                    appendPolygonPoint(startPos)
                    event.accepted = true
                }
            } else if (state === "idle" && event.button === Qt.RightButton) {

            } else if (state === "idle" && event.button === Qt.LeftButton) {
                // 获取相对于LabelImage的坐标
                startPos = getPosOnImage(event)
                state = hitTest(startPos) ? "readyEdit" : "readyDraw"
                imageLabelsList.setHovered([])
            } 
        }

        onReleased: function(event) {
            if (suppressNextRelease) {
                suppressNextRelease = false
                state = "idle"
                return
            }
            if (segmentationMode && drawingPolygon && state === "idle") {
                updatePolygonPreview(getPosOnImage(event))
                return
            }
            if (state === "drawing") {
                data = drawingItem.getData()
                // 添加到ListModel
                if (dataManager && labelClasses.currentLabelClassId !== -1 && data.width > 1 && data.height > 1) {
                    dataManager.addLabels([imageInstances.currentImageId], [labelClasses.currentLabelClassId], [data])
                }
                drawingItem.clearItem()
            } else if (state === "draging") {
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
            if (segmentationMode && drawingPolygon && state === "idle") {
                updatePolygonPreview(getPosOnImage(event))
                return
            }
            if (state === "readyEdit") {
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
            } else {
                let pos = getPosOnImage(event)
                if (!hitTest(pos)) {
                    mouseArea.cursorShape = Qt.ArrowCursor
                    let indices = imageLabelsList.getIndicesAt(pos)
                    imageLabelsList.setHovered(indices)
                }
            }
        }

        onEntered: {
            labelView.forceActiveFocus()
        }

        onDoubleClicked: function(event) {
            if (segmentationMode && event.button === Qt.LeftButton && drawingPolygon) {
                finishPolygonDrawing()
                suppressNextRelease = true
                event.accepted = true
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
            dataManager.addLabels([imageInstances.currentImageId],
                                  [labelClasses.currentLabelClassId],
                                  [{points: points}])
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

    function moveImage(event) {
        let dx = event.x - startPos.x
        let dy = event.y - startPos.y
        labelImage.image.x += dx
        labelImage.image.y += dy
        startPos = Qt.point(event.x, event.y)
    }

    function getPosOnImage(event) {
        return Qt.point((event.x - labelImage.image.x) / labelImage.image.scale, (event.y - labelImage.image.y) / labelImage.image.scale)
    }

    function hitTest(pos) {
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
