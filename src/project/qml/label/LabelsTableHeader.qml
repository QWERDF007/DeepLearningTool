import QtQuick 
import QtQuick.Controls
import QtQuick.Layouts 

import dltool.ui


HorizontalHeaderView {
    id: horizontalHeader
    clip: true
    columnSpacing: 5
    resizableColumns: true
    boundsBehavior: Flickable.StopAtBounds
    delegate:  Rectangle {
        implicitWidth: colWidth
        implicitHeight: horizontalHeader.height
        color: DltColor.Background
        Rectangle {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: parent.height - 4
            color: DltColor.Primary
        }

        DltText {
            anchors.centerIn: parent
            text: horizontalHeader.textRole ?
                        (Array.isArray(horizontalHeader.model) ? modelData[horizontalHeader.textRole] : model[horizontalHeader.textRole])
                    : modelData
            color: "white"
        }
    }
}
