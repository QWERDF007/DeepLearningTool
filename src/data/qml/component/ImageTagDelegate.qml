import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui
    
Rectangle {
    id: control
    width: 80
    height: 30

    property int tagId: -1
    property string tagName: ""
    property string tagStats: ""

    color: tagStats === "" ? Qt.lighter(QuiColor.Primary, 1.2) : QuiColor.Highlight

    signal clicked()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        QuiText {
            id: name
            Layout.fillWidth: true
            // anchors.left: parent.left
            // anchors.leftMargin: 5
            // anchors.right: parent.right
            // anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: tagName
        }
        QuiText {
            id: stat
            Layout.preferredWidth: 20
            // width: 20
            // anchors.right: parent.right
            // anchors.rightMargin: 5
            // anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignRight
            text: tagStats
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            control.clicked()
        }
    }
    QuiToolTip {
        text: tagName
        visible: name.truncated && mouse.containsMouse
        delay: 200
    }
}
