import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

DltPopup {
    Item {
        anchors.fill: parent
        anchors.margins: 10
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 2
            color: DltColor.Background
        }
    }
    T.Overlay.modal: null
}