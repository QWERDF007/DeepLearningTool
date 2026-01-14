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
    property bool hasLabels: false
    
    signal clicked()
    
    Row {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 4
        
        // 是否标注图标
        Item {
            width: 16
            height: parent.height
            DltTextIcon {
                id: labelIcon
                anchors.fill: parent
                visible: delegate.hasLabels
                iconSource: DltFontIcon.CheckMark
                iconSize: 16
                iconColor: "green"
            }
        }
        
        DltText {
            id: pathText
            width: parent.width - (labelIcon.visible ? labelIcon.width + parent.spacing : 0)
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            text: delegate.filePath
            elide: Text.ElideLeft
            font: DltFont.Body
        }
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
