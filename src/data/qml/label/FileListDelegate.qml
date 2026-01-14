import QtQuick
import QtQuick.Controls

import dltool.ui

Rectangle {
    id: delegate
    height: 24
    radius: 4
    color: selected ? DltColor.Highlight : "transparent"
    
    property string filePath: ""
    property bool selected: false
    
    signal clicked()
    
    DltText {
        id: pathText
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        verticalAlignment: Text.AlignVCenter
        text: delegate.filePath
        elide: Text.ElideLeft
        font: DltFont.Body
    }
    
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: delegate.clicked()
    }
    
    DltToolTip {
        text: delegate.filePath
        visible: mouseArea.containsMouse && pathText.truncated
        delay: 500
    }
}
