import QtQuick
import QtQuick.Controls

import dltool.ui

DltLoader {
    id: drawItemLoader

    property real offsetX: 0
    property real offsetY: 0
    property real factor: 1

    property real ix: 0
    property real iy: 0
    property real iwidth: 0
    property real iheight: 0
    
    Component {
        id: rectItem
        Rectangle {
            x: ix * factor + offsetX
            y: iy * factor + offsetY
            width: iwidth * factor
            height: iheight * factor
            color: "transparent"
            border.color: "red"
            border.width: 2
        }
    }

    sourceComponent: rectItem

    function initItem(x, y, width, height, color) {
        drawItemLoader.item.visible = true
        drawItemLoader.ix = x
        drawItemLoader.iy = y
        drawItemLoader.iwidth = width
        drawItemLoader.iheight = height
        drawItemLoader.item.border.color = color
    }

    function updateItem(x, y, width, height) {
        drawItemLoader.ix = x
        drawItemLoader.iy = y
        drawItemLoader.iwidth = width
        drawItemLoader.iheight = height
    }

    function clearItem() {
        drawItemLoader.item.visible = false
        drawItemLoader.iwidth = 0
        drawItemLoader.iheight = 0
    }
}

    
