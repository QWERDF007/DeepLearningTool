import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    id: imageInstanceDelegate
    property alias image: image
    property int image_id: -1
    property bool selected: false

    width: 320
    height: 240
    color: "transparent"
    border.color: selected ? DltColor.Highlight : DltColor.Border
    border.width: selected ? 2 : 1


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
