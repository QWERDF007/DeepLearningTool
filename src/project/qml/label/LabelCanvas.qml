import QtQuick
import QtQuick.Controls

import dltool.project

Item {
    id: labelView

    property Project project: ProjectManager.currentProject

    ListModel {
        id: labelModel
    }

    LabelImage {
        id: labelImage
        anchors.fill: parent
        curImagePath: project ? project.imageInstances.curImagePath : ""

        Repeater {
            model: labelImage.image.status === Image.Ready ? labelModel : null
            Rectangle {
                x: model.x
                y: model.y
                width: model.width
                height: model.height
                color: "transparent"
                border.color: model.color
                border.width: 2
            }
        }

        Loader {
            id: drawItemLoader
            sourceComponent: rectItem
        }
    }

    Component {
        id: rectItem
        Rectangle {
            width: 0
            height: 0
            color: "transparent"
            border.color: "red"
            border.width: 2
        }
    }

    CrosshairCanvas {
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
                var pos = mapToItem(labelImage.image, event.x, event.y)
                startPoint = Qt.point(pos.x, pos.y)
                isDrawing = true
                drawItemLoader.item.visible = true
                drawItemLoader.item.x = startPoint.x
                drawItemLoader.item.y = startPoint.y
                drawItemLoader.item.width = 0
                drawItemLoader.item.height = 0    
            } else if (event.button === Qt.RightButton) {
                isDrawing = false
            } else {
                isDrawing = false
            }
        }

        onReleased: function(event) {
            if (isDrawing) {
                isDrawing = false
                drawItemLoader.item.visible = false
                
                // 获取相对于LabelImage的坐标
                var pos = mapToItem(labelImage.image, event.x, event.y)
                
                // 计算矩形的位置和大小
                var x = Math.min(startPoint.x, pos.x)
                var y = Math.min(startPoint.y, pos.y)
                var width = Math.abs(pos.x - startPoint.x)
                var height = Math.abs(pos.y - startPoint.y)

                // 添加到ListModel
                labelModel.append({
                    "x": x,
                    "y": y, 
                    "width": width,
                    "height": height,
                    "color": "red"
                })

                drawItemLoader.item.width = 0
                drawItemLoader.item.height = 0
            }
        }

        onPositionChanged: function(event) {
            if (isDrawing) {
                // 获取相对于LabelImage的坐标
                var pos = mapToItem(labelImage.image, event.x, event.y)
                
                // 更新绘制中的矩形
                var x = Math.min(startPoint.x, pos.x)
                var y = Math.min(startPoint.y, pos.y)
                var width = Math.abs(pos.x - startPoint.x)
                var height = Math.abs(pos.y - startPoint.y)

                drawItemLoader.item.x = x
                drawItemLoader.item.y = y
                drawItemLoader.item.width = width 
                drawItemLoader.item.height = height
            }
        }
    }
}
