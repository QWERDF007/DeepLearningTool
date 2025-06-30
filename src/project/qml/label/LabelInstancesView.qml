import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle {
    color: DltColor.Primary
    width: 200
    height: 200

    property Project project: ProjectManager.currentProject
    property ImageLabelsModel imageLabels: project ? project.imageLabels : null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 5
        DltText {
            text: "标签实例:"
            font: DltFont.Subtitle
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true

            HorizontalHeaderView {
                id: horizontalHeader
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                model: ["类别", "X", "Y", "宽度", "高度"]
                syncView: tableView
                columnSpacing: 5
                resizableColumns: true
                resizableRows: true
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                delegate:  Rectangle {
                    implicitWidth: horizontalHeader.width / horizontalHeader.columns
                    implicitHeight: horizontalHeader.height
                    //                    implicitHeight: horizontalHeader.implicitHeight
                    color: DltColor.Background
                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 2
                        height: parent.height - 4
                        color: DltColor.Primary
                    }

                    DltText {
                        id: text
                        anchors.centerIn: parent
                        text: horizontalHeader.textRole ?
                            (Array.isArray(horizontalHeader.model) ? modelData[horizontalHeader.textRole] : model[horizontalHeader.textRole])
                            : modelData
                        color: "white"
                    }
                }
            }

            TableView {
                id: tableView
                Layout.fillWidth: true
                Layout.fillHeight: true
                boundsBehavior: Flickable.StopAtBounds

                model: 10
                delegate: Rectangle {
                    implicitWidth: tableView.width / 5
                    implicitHeight: 24
                    color: index % 2 == 0 ? "green" : "blue"
                }
            }
        }
    }
}
