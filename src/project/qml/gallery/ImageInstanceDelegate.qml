import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    id: imageInstanceDelegate
    width: 320
    height: 240
    color: "transparent"
    border.color: DltColor.Border
    border.width: 1
    property alias image: image

    Image {
        id: image
        anchors.fill: parent
        anchors.margins: 2
        fillMode: Image.PreserveAspectFit
        // width: parent.width
        // height: parent.height
        sourceSize.width: parent.width
        sourceSize.height: parent.height
        asynchronous: true
    }
}
