import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    id: modelView
    width: 200
    height: 200
    color: DltColor.Primary
    property alias headerTitle: header.text
    property alias addEnable: header.addEnable

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        ModelHeader {
            id: header
            Layout.fillWidth: true
            height: 32
        }

        ListView {
            id: view
            clip: true
            Layout.fillHeight: true
            Layout.fillWidth: true
            boundsBehavior: Flickable.StopAtBounds
            // model: models
            model: 5
            ScrollBar.vertical: DltScrollBar {}

            delegate: ModelDelegate {
                height: 200
                width: view.width
            }
        }
    }
}