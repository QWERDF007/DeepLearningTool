import QtQuick 
import QtQuick.Controls
import QtQuick.Layouts 

import dltool.ui


Rectangle {
    clip: true
    property var mdata
    property bool selected
    color: selected ? DltColor.Highlight : row % 2 == 0 ? Qt.lighter(DltColor.Primary, 1.3) : DltColor.Primary
    DltText {
        anchors.fill: parent
        anchors.leftMargin: 2
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        text: mdata.toFixed(2)
        verticalAlignment: Text.AlignVCenter
    }
}