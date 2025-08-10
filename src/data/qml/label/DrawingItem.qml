import QtQuick
import QtQuick.Controls

import dltool.ui

DltLoader {
    id: drawItemLoader

    property int labelId: -1

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

    function initItem(data) {
        drawItemLoader.item.visible = true
        drawItemLoader.labelId = data.label_id
        drawItemLoader.ix = data.x
        drawItemLoader.iy = data.y
        drawItemLoader.iwidth = data.width
        drawItemLoader.iheight = data.height
        drawItemLoader.item.border.color = data.color
    }

    function updateItem(data) {
        drawItemLoader.labelId = data.label_id
        drawItemLoader.ix = data.x
        drawItemLoader.iy = data.y
        drawItemLoader.iwidth = data.width
        drawItemLoader.iheight = data.height
    }

    function clearItem() {
        drawItemLoader.item.visible = false
        drawItemLoader.iwidth = 0
        drawItemLoader.iheight = 0
    }

    function getData() {
        return {
            label_id: drawItemLoader.labelId,
            x: drawItemLoader.ix,
            y: drawItemLoader.iy,
            width: drawItemLoader.iwidth,
            height: drawItemLoader.iheight,
            color: drawItemLoader.item.border.color
        }
    }
}

    
