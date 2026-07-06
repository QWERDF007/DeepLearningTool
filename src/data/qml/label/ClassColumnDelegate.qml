import QtQuick 
import QtQuick.Controls
import QtQuick.Layouts 

import dltool.ui
import quickui

Rectangle {
    clip: true
    property var mdata
    property bool selected
    color: rowBackgroundColor()
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 2
        anchors.rightMargin: 2
        Rectangle {
            color: mdata.class_color
            width: rowHeight - 5
            height: rowHeight - 5
            radius: 3
            border.width: 1
            border.color: "black"
        }
        QuiText {
            Layout.fillWidth: true
            Layout.fillHeight: true
            elide: Text.ElideRight
            text:  mdata.class_name
            verticalAlignment: Text.AlignVCenter
        }    
    }   

    function rowBackgroundColor() {
        if (selected) {
            return QuiColor.Highlight
        }
        if (row % 2 == 0) {
            return Qt.lighter(QuiColor.Primary, 1.3)
        }
        return QuiColor.Primary
    }
}
