import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import dltool.ui
import dltool.project

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
        visible: false
        anchors.fill: parent
        anchors.margins: 2
        fillMode: Image.PreserveAspectFit
        sourceSize.width: image.width
        sourceSize.height: image.height
        asynchronous: true
    }
    
    BusyIndicator {
        anchors.centerIn: parent
        running: image.status === Image.Loading
    }

    MultiEffect {
        source: image
        anchors.fill: image
        brightness: Settings.imageBrightness
        contrast: Settings.imageContrast
    }
}
