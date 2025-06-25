import QtQuick
import QtQuick.Controls

import dltool.project

Item {
    id: labelView

    property Project project: ProjectManager.currentProject
    property ImageInstancesModel imageInstances: project ? project.imageInstances : null
    property LabelClassesModel labelClasses: project ? project.labelClasses : null
    property ImageLabelsModel imageLabels: project ? project.imageLabels : null
    property color drawingColor: project ? labelClasses.currentLabelClassColor : "red"

    LabelImage {
        id: labelImage
        anchors.fill: parent
        curImagePath: project ? project.imageInstances.curImagePath : ""
    }

    LabelsView {
        offsetX: labelImage.image.x
        offsetY: labelImage.image.y
        factor: labelImage.image.scale
        model: labelImage.image.status === Image.Ready ? imageLabels : null
    }

    DrawingItem {
        id: drawingItem
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
        property point startPoint
        property bool isDrawing: false

        onPressed: function(event) {
            if (event.button === Qt.LeftButton) {
                // 获取相对于LabelImage的坐标
                startPoint = Qt.point(event.x, event.y)
                isDrawing = true
                drawingItem.initItem(startPoint.x, startPoint.y, 0, 0, drawingColor)
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
                var x = (Math.min(startPoint.x, event.x) - labelImage.image.x) / labelImage.image.scale
                var y = (Math.min(startPoint.y, event.y) - labelImage.image.y) / labelImage.image.scale
                var width = Math.abs(event.x - startPoint.x) / labelImage.image.scale
                var height = Math.abs(event.y - startPoint.y) / labelImage.image.scale

                // 添加到ListModel
                if (project) {
                    let data = { "x": x, "y": y, "width": width, "height": height }
                    project.addLabel(imageInstances.curImageId, labelClasses.currentLabelClassId, data)
                }
            }
        }

        onPositionChanged: function(event) {
            if (isDrawing) {
                // 更新绘制中的矩形
                var x = Math.min(startPoint.x, event.x)
                var y = Math.min(startPoint.y, event.y)
                var width = Math.abs(event.x - startPoint.x)
                var height = Math.abs(event.y - startPoint.y)

                drawingItem.updateItem(x, y, width, height)
            }
        }
    }
}
