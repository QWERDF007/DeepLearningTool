import QtQuick
import QtQuick.Controls

import dltool.ui
import dltool.project

Item {
    id: labelView

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
    }

    LabelImage {
        id: labelImage
        anchors.fill: parent
        curImagePath: project ? project.imageInstances.curImagePath : ""
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
        visible: mouseArea.containsMouse && !labelImage.isDragging && labelImage.image.status === Image.Ready
        mousePos: Qt.point(mouseArea.mouseX, mouseArea.mouseY)
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        property bool isDrawing: false

        onPressed: function(event) {
            if (event.button === Qt.LeftButton) {
                // 获取相对于LabelImage的坐标
                startPos = Qt.point((event.x - labelImage.image.x) / labelImage.image.scale, (event.y - labelImage.image.y) / labelImage.image.scale)
                isDrawing = true
                drawingItem.initItem(startPos.x, startPos.y, 0, 0, drawingColor)
            } else if (event.button === Qt.RightButton) {
                isDrawing = false
            } else {
                isDrawing = false
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
                    project.addLabels([imageInstances.curImageId], [labelClasses.currentLabelClassId], [rect])
                }
            }
        }

        onPositionChanged: function(event) {
            if (isDrawing) {
                let rect = getRect(event)
                drawingItem.updateItem(rect.x, rect.y, rect.width, rect.height)
            }
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
}
