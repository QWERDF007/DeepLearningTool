import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    id: imageTagView
    color: DltColor.Primary
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        ImageTagHeader {
            Layout.fillWidth: true
            height: 32
            // project: imageTagView.project
        }

        GridView {
            Layout.fillHeight: true
            Layout.fillWidth: true
            cellWidth: 90
            cellHeight: 40
            model: 10
            delegate: Item {
                width: 80
                height: 30
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 5
                    color: Qt.lighter(DltColor.Primary, 1.2)
                }
            }
        }
    }
}
