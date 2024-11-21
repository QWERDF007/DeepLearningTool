import QtQuick
import QtQuick.Controls

import dltool.ui

Item {
    property int radius: 4
    id:control
    anchors.fill: parent
    Rectangle{
        width: control.width
        height: control.height
        anchors.centerIn: parent
        color: "#00000000"
        border.width: 2
        radius: control.radius
        border.color:  DltColor.Highlight // Qt.rgba(0,0,0,1)
        z: 65535
    }
}
