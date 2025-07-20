import QtQuick
import QtQuick.Controls

import dltool.ui
import dltool.project

Item {
    id: labelView
    clip: true

    property Project project: ProjectManager.currentProject
    property ImageInstancesModel imageInstances: project ? project.imageInstances : null
    property LabelClassesModel labelClasses: project ? project.labelClasses : null
    property ImageLabelsListModel imageLabelsList: project ? project.imageLabelsList : null
    property ItemSelectionModel selection: imageLabelsList ? imageLabelsList.selection : null
    property color drawingColor: project ? labelClasses.currentLabelClassColor : "red"
    property point startPos: Qt.point(0, 0)

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

    LabelImage {
        id: labelImage
        anchors.fill: parent
        currentImagePath: project ? project.imageInstances.currentImagePath : ""
    }

    LabelsListView {
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
        property bool isDrawing: false
        property bool isDragging: false

        onPressed: function(event) {
            if (event.button === Qt.MiddleButton || (event.modifiers & Qt.ControlModifier && event.button === Qt.LeftButton)) {
                mouseArea.cursorShape = Qt.ClosedHandCursor
                startPos = Qt.point(event.x, event.y)
                isDragging = true
                return
            } else if (event.button === Qt.RightButton) {

            } else if (event.button === Qt.LeftButton) {
                isDrawing = true
                // 获取相对于LabelImage的坐标
                startPos = Qt.point((event.x - labelImage.image.x) / labelImage.image.scale, (event.y - labelImage.image.y) / labelImage.image.scale)
                drawingItem.initItem(startPos.x, startPos.y, 0, 0, drawingColor)
            } else {
                isDrawing = false
                isDragging = false
            }
        }

        onReleased: function(event) {
            if (isDrawing) {
                isDrawing = false
                drawingItem.clearItem()

                // 计算矩形的位置和大小
                let rect = getRect(event)

                // 添加到ListModel
                if (project && labelClasses.currentLabelClassId !== -1 && rect.width > 1 && rect.height > 1) {
                    project.addLabels([imageInstances.currentImageId], [labelClasses.currentLabelClassId], [rect])
                }
            } else if (isDragging) {
                isDragging = false
                mouseArea.cursorShape = event.modifiers & Qt.ControlModifier ? Qt.OpenHandCursor : Qt.ArrowCursor
                startPos = Qt.point(event.x, event.y)
                return
            } else {
                mouseArea.cursorShape = event.modifiers & Qt.ControlModifier ? Qt.OpenHandCursor : Qt.ArrowCursor
            }
        }

        onPositionChanged: function(event) {
            if (isDrawing) {
                let rect = getRect(event)
                drawingItem.updateItem(rect.x, rect.y, rect.width, rect.height)
            }  else if (isDragging) {
                let dx = event.x - startPos.x
                let dy = event.y - startPos.y
                labelImage.image.x += dx
                labelImage.image.y += dy
                startPos = Qt.point(event.x, event.y)
            } else {
                let pos = Qt.point((event.x - labelImage.image.x) / labelImage.image.scale, (event.y - labelImage.image.y) / labelImage.image.scale)
                imageLabelsList.hover(pos)
            }
        }
        onEntered: {
            labelView.forceActiveFocus()
        }
    }

    function getRect(event) {
        // 计算矩形的位置和大小
        let endPos = Qt.point((event.x - labelImage.image.x) / labelImage.image.scale, (event.y - labelImage.image.y) / labelImage.image.scale)
        let x = Math.min(startPos.x, endPos.x)
        let y = Math.min(startPos.y, endPos.y)
        let width = Math.abs(endPos.x - startPos.x)
        let height = Math.abs(endPos.y - startPos.y)
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
}
