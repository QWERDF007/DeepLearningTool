import QtQuick
import QtQuick.Controls

import dltool.ui

DltLoader {
    id: drawItemLoader

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

    sourceComponent: rectItem

    function initItem(x, y, width, height, color) {
        drawItemLoader.item.visible = true
        drawItemLoader.item.x = x
        drawItemLoader.item.y = y
        drawItemLoader.item.width = width
        drawItemLoader.item.height = height
        drawItemLoader.item.border.color = color
    }

    function updateItem(x, y, width, height) {
        drawItemLoader.item.x = x
        drawItemLoader.item.y = y
        drawItemLoader.item.width = width
        drawItemLoader.item.height = height
    }

    function clearItem() {
        drawItemLoader.item.visible = false
        drawItemLoader.item.width = 0
        drawItemLoader.item.height = 0
    }
}

    
