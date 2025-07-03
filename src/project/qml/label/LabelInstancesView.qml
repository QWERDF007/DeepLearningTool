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
    property ImageLabelsTableModel imageLabelsTable: project ? project.imageLabelsTable : null

    function isNumber(value) {
        return typeof value === "number" && !isNaN(value);
    }

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
                clip: true
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                syncView: tableView
                columnSpacing: 5
                resizableColumns: true
                boundsBehavior: Flickable.StopAtBounds
                delegate:  Rectangle {
                    implicitWidth: horizontalHeader.width / horizontalHeader.columns
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

            TableView {
                id: tableView
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true
                boundsBehavior: Flickable.StopAtBounds

                model: imageLabelsTable
                delegate: Rectangle {
                    clip: true
                    property var mdata: model.data
                    implicitWidth: horizontalHeader.width / horizontalHeader.columns
                    implicitHeight: 24
                    color: row % 2 == 0 ? Qt.lighter(DltColor.Primary, 1.3) : DltColor.Primary
                    DltText {
                        width: parent.width
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        text: isNumber(mdata) ? mdata.toFixed(2) : mdata
                    }
                }
            }
        }
    }
}
