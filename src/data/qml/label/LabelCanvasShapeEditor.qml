import QtQuick
import QtQml.Models

import dltool.core
import dltool.data
import dltool.ui
import quickui

Item {
    id: editor
    visible: false

    property var canvas
    property var labelsListView
    property var drawingItem
    property var actions
    property bool selectToolMode: false
    property bool rectangleToolMode: false
    property var addLabelHandler: function(data) { return false }

    readonly property ImageLabelsListModel imageLabelsList: canvas ? canvas.imageLabelsList : null
    readonly property ItemSelectionModel selection: canvas ? canvas.selection : null

    function handleKeyPressed(event) {
        if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
            if (imageLabelsList) {
                imageLabelsList.selectAll()
                SignalHelper.imageLabelListSelectAll()
            }
            return true
        }

        if (event.key === Qt.Key_Delete && selection && selection.hasSelection) {
            if (actions) {
                actions.deleteSelectedLabels()
            }
            return true
        }

        if (event.key === Qt.Key_Escape
                && rectangleToolMode
                && canvas
                && canvas.interactionState === LabelCanvasEnums.Idle) {
            canvas.setToolMode(LabelCanvasEnums.SelectTool)
            event.accepted = true
            return true
        }

        return false
    }

    function beginToolPress(pos) {
        if (!canvas) {
            return
        }

        canvas.startPos = pos
        if (rectangleToolMode) {
            canvas.interactionState = LabelCanvasEnums.ReadyDraw
            clearSelection()
        } else if (selectToolMode) {
            canvas.interactionState = hitTest(canvas.startPos) ? LabelCanvasEnums.ReadyEdit : LabelCanvasEnums.Idle
        } else {
            canvas.interactionState = LabelCanvasEnums.Idle
        }
        if (imageLabelsList) {
            imageLabelsList.setHovered([])
        }
    }

    function handleMouseReleased(event) {
        if (!canvas) {
            return false
        }

        if (!selectToolMode
                && canvas.interactionState !== LabelCanvasEnums.Drawing
                && canvas.interactionState !== LabelCanvasEnums.Dragging) {
            canvas.setIdleCursor(event.modifiers)
            canvas.interactionState = LabelCanvasEnums.Idle
            return true
        }

        if (canvas.interactionState === LabelCanvasEnums.Drawing) {
            finishRectangleDrawing()
        } else if (canvas.interactionState === LabelCanvasEnums.Editing) {
            finishLabelEditing(event)
        } else if (canvas.interactionState === LabelCanvasEnums.ReadyEdit) {
            finishReadyEdit(event)
        } else {
            handleIdleRelease(event)
        }
        canvas.interactionState = LabelCanvasEnums.Idle
        return true
    }

    function handleMousePositionChanged(event, pos) {
        if (!canvas) {
            return false
        }

        if (selectToolMode && canvas.interactionState === LabelCanvasEnums.ReadyEdit) {
            beginLabelEditing(pos)
        } else if (canvas.interactionState === LabelCanvasEnums.ReadyDraw) {
            beginRectangleDrawing()
        }

        updateActiveMouseState(pos)
        return true
    }

    function finishRectangleDrawing() {
        if (!drawingItem) {
            return
        }

        canvas.activeData = drawingItem.getData()
        if (canvas.rectangleDrawingUsesPolygon) {
            canvas.activeData = canvas.rectangleDataToPolygon(canvas.activeData)
        }
        if (canvas.activeData.width > 1 && canvas.activeData.height > 1) {
            addLabelHandler(canvas.activeData)
        }
        drawingItem.clearItem()
    }

    function finishLabelEditing(event) {
        let item = labelsListView ? labelsListView.itemAt(canvas.activeData.index) : null
        if (canvas.dataManager && canvas.activeData.label_id !== -1) {
            canvas.dataManager.updateLabels([canvas.activeData.label_id], [canvas.activeData])
        }
        let pos = canvas.geometry.getPosOnImage(event)
        if (!hitTest(pos)) {
            canvas.canvasMouseArea.cursorShape = Qt.ArrowCursor
        }
        if (item) {
            item.visible = true
        }
        if (drawingItem) {
            drawingItem.clearItem()
        }
    }

    function finishReadyEdit(event) {
        let pos = canvas.geometry.getPosOnImage(event)
        let hit = hitTest(pos)
        if (hit && hit.found && (hit.edge_index !== undefined || hit.mode === 1)) {
            return
        }
        if (event.button === Qt.LeftButton) {
            canvas.setIdleCursor(event.modifiers)
            selectAt(pos)
        }
    }

    function handleIdleRelease(event) {
        canvas.setIdleCursor(event.modifiers)
        let pos = canvas.geometry.getPosOnImage(event)
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
        if (!imageLabelsList || !actions) {
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
        actions.popup()
    }

    function beginLabelEditing(pos) {
        canvas.interactionState = LabelCanvasEnums.Editing
        canvas.canvasMouseArea.cursorShape = Qt.SizeAllCursor
        let hit = hitTest(pos)
        let index = imageLabelsList.getTopSelectedIndex()
        let item = labelsListView ? labelsListView.itemAt(index) : null
        if (item) {
            item.visible = false
        }
        canvas.activeData = imageLabelsList.getData(index)
        canvas.activeData.hit = hit
        if (drawingItem) {
            drawingItem.initItem(canvas.activeData)
        }
    }

    function beginRectangleDrawing() {
        canvas.interactionState = LabelCanvasEnums.Drawing
        if (drawingItem) {
            drawingItem.initItem(newDraftLabelData({x: canvas.startPos.x, y: canvas.startPos.y,
                                                   width: 0, height: 0}))
        }
    }

    function updateActiveMouseState(pos) {
        if (canvas.interactionState === LabelCanvasEnums.Drawing) {
            updateRectangleDrawing(pos)
        } else if (canvas.interactionState === LabelCanvasEnums.Editing) {
            updateLabelEditing(pos)
        } else if (selectToolMode) {
            updateSelectionHover(pos)
        } else if (imageLabelsList) {
            imageLabelsList.setHovered([])
        }
    }

    function updateRectangleDrawing(pos) {
        if (!drawingItem) {
            return
        }

        let rect = canvas.geometry.rectFromPoints(canvas.startPos, pos)
        drawingItem.updateItem(newDraftLabelData(rect))
    }

    function updateLabelEditing(pos) {
        if (!drawingItem) {
            return
        }

        canvas.activeData = imageLabelsList.getEditedData(canvas.activeData, canvas.startPos, pos)
        drawingItem.updateItem(canvas.activeData)
        canvas.startPos = pos
    }

    function updateSelectionHover(pos) {
        if (!imageLabelsList) {
            return
        }
        if (!hitTest(pos)) {
            canvas.canvasMouseArea.cursorShape = Qt.ArrowCursor
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

    function hitTest(pos) {
        if (!selectToolMode || !imageLabelsList) {
            return null
        }
        if (selection === null || !selection.hasSelection) {
            return null
        }

        let selectedIndex = imageLabelsList.getTopSelectedIndex()
        if (selectedIndex !== -1) {
            let hit = imageLabelsList.hitTestHandle(pos, selectedIndex, canvas.imageView.image.scale)
            if (hit.found) {
                canvas.canvasMouseArea.cursorShape = hit.cursor
                return hit
            }
        }
        return null
    }

    function newDraftLabelData(fields) {
        let data = {label_id: -1, color: canvas ? canvas.drawingColor : "red"}
        for (let key in fields) {
            data[key] = fields[key]
        }
        return data
    }
}
