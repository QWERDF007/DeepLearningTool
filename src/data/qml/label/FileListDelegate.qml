import QtQuick
import QtQuick.Controls

import dltool.ui
import quickui

Rectangle {
    id: delegate
    height: 24
    radius: 4
    color: selected ? QuiColor.Highlight : QuiColor.Transparent
    
    property string filePath: ""
    property bool selected: false
    property bool hasLabels: false
    
    signal clicked()
    signal rightClicked()
    
    Row {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 4
        
        // 是否标注图标
        Item {
            width: 16
            height: parent.height
            QuiTextIcon {
                id: labelIcon
                anchors.fill: parent
                visible: delegate.hasLabels
                iconSource: QuiFontIcon.CheckMark
                iconSize: 16
                iconColor: "green"
            }
        }
        
        QuiText {
            id: pathText
            width: parent.width - (labelIcon.visible ? labelIcon.width + parent.spacing : 0)
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            text: delegate.filePath
            elide: Text.ElideLeft
            font: QuiFont.Body
        }
    }
    
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                delegate.rightClicked()
            } else {
                delegate.clicked()
            }
        }
    }
    
    QuiToolTip {
        text: delegate.filePath
        visible: mouseArea.containsMouse && pathText.truncated
        delay: 500
    }
}
