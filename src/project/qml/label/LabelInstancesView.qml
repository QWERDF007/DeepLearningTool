import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.qmlmodels

import dltool.ui
import dltool.project

Rectangle {
    id: control
    color: DltColor.Primary
    width: 200
    height: 200

    property Project project: ProjectManager.currentProject
    property ImageLabelsTableModel imageLabelsTable: project ? project.imageLabelsTable : null
    property ItemSelectionModel selection: imageLabelsTable ? imageLabelsTable.selection : null

    property real rowHeight: 24
    property real colWidth: horizontalHeader.width / horizontalHeader.columns

    signal tableSelectionChanged(var index, var command)

    function isNumber(value) {
        return typeof value === "number" && !isNaN(value);
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        // spacing: 5
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

            TableView {
                id: tableView
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true
                boundsBehavior: Flickable.StopAtBounds

                model: imageLabelsTable
                // delegate: Rectangle {
                //     clip: true
                //     property var mdata: model.data
                //     implicitWidth: horizontalHeader.width / horizontalHeader.columns
                //     implicitHeight: 24
                //     color: row % 2 == 0 ? Qt.lighter(DltColor.Primary, 1.3) : DltColor.Primary
                //     DltText {
                //         width: parent.width
                //         anchors.verticalCenter: parent.verticalCenter
                //         elide: Text.ElideRight
                //         text: isNumber(mdata) ? mdata.toFixed(2) : mdata
                //     }
                // }

                delegate: DelegateChooser {
                    // role: "column"

                    DelegateChoice{
                        column: 0
                        Rectangle {
                            clip: true
                            implicitWidth: colWidth
                            implicitHeight: rowHeight
                            color: model.selected ? DltColor.Highlight : row % 2 == 0 ? Qt.lighter(DltColor.Primary, 1.3) : DltColor.Primary
                            DltText {
                                width: parent.width
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                text:  model.data
                            }
                        }
                    }

                    DelegateChoice {
                        Rectangle {
                            clip: true
                            implicitWidth: colWidth
                            implicitHeight: rowHeight
                            color: model.selected ? DltColor.Highlight : row % 2 == 0 ? Qt.lighter(DltColor.Primary, 1.3) : DltColor.Primary
                            DltText {
                                width: parent.width
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                text: model.data.toFixed(2)
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(mouse) {
                        tableView.forceActiveFocus()

                        let row = Math.floor(mouse.y / rowHeight)
                        if (selection) {
                            let tmpIndex = tableView.model.index(row, 0)
                            control.select(tmpIndex, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                        }
                    }
                }
            }
        }
    }

    function select(index, command) {
        if (selection) {
            selection.select(index, command)
            control.tableSelectionChanged(index, command)
        }
    }
}
