import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Item {
    id: methodSelection
    width: 400
    height: 600
    // clip: true
    property int method: view.currentItem ? view.currentItem.method : -1
    property alias methods: view.model
    ColumnLayout {
        anchors.fill: parent
        DltText {
            text: "深度学习方法"
            font: DltFont.Body
            textColor: DltColor.FontDark
        }

        GridView {
            id: view
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Layout.fillHeight: true
            Layout.fillWidth: true
            cellWidth: view.width / 4
            cellHeight: view.height / 3
            delegate: Item {
                width: view.cellWidth - 10
                height: view.cellHeight - 10
                property int method: modelData.method
                Rectangle {
                    anchors.fill: parent
                    color: DltColor.Primary
                    border.width: view.currentIndex == index ? 3 : 0
                    border.color: DltColor.Highlight
                    DltText {
                        anchors.centerIn: parent
                        text: modelData.name
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        view.currentIndex = index
                    }
                }
            }
        }
    }
}
