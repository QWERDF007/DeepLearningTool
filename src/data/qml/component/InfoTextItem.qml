import QtQuick
import QtQuick.Layouts

import dltool.ui

ColumnLayout {
    id: root
    
    property string title: ""
    property string text: ""
    property bool showTooltip: true
    
    signal clicked()
    
    spacing: 2
    
    DltText {
        Layout.leftMargin: 5
        Layout.rightMargin: 5
        text: root.title
        textColor: DltColor.FontDark
    }
    
    DltText {
        id: contentText
        Layout.fillWidth: true
        Layout.leftMargin: 5
        Layout.rightMargin: 5
        text: root.text
        elide: Text.ElideMiddle
        
        DltToolTip {
            text: contentText.text
            delay: 200
            visible: root.showTooltip && mouseArea.containsMouse && contentText.truncated
        }
        
        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.RightButton
            onClicked: root.clicked()
        }
    }
}
