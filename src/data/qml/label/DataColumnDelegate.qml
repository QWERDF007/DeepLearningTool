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
    QuiText {
        anchors.fill: parent
        anchors.leftMargin: 2
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        text: typeof mdata === "number" && isFinite(mdata) ? mdata.toFixed(2) : ""
        verticalAlignment: Text.AlignVCenter
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
