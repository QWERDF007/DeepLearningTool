import QtQuick
import QtQuick.Controls

import dltool.ui
import quickui

TabBar {
    id: control
    width: 200
    height: 48
    property color bgColor: QuiColor.Primary
    property color highlightColor: QuiColor.Highlight
    background: Rectangle {
        color: bgColor
        height: control.height
    }

    spacing: 1

    contentItem: ListView {
        id: lv
        height: control.height
        currentIndex: control.currentIndex
        model: control.contentModel
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        snapMode: ListView.SnapToItem

        highlightFollowsCurrentItem: false
        highlight: Rectangle {
            x: lv.currentItem.x
            z: 2
            y: lv.height - height - 1
            height: 2
            width: 100
            color: highlightColor
            Behavior on x {
                SmoothedAnimation {
                    duration: 300
                    velocity: 500
                }
            }
        }
    }
}



