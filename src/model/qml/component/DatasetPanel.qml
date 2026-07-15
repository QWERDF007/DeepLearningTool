import QtQuick
import QtQuick.Layouts

import dltool.data
import dltool.model
import dltool.ui
import quickui

Item {
    id: control

    property DataManager dataManager: null
    property DataSelectionTreeModel selectionModel: null
    property string roleTitle: qsTr("数据集")
    property int treeHeight: 180
    property int partSpacing: 5

    implicitHeight: content.implicitHeight + control.partSpacing * 2

    DatasetSelectionStatisticsModel {
        id: statisticsModel
        dataManager: control.dataManager
        selectionModel: control.selectionModel
    }

    ColumnLayout {
        id: content

        anchors.fill: parent
        anchors.margins: control.partSpacing
        spacing: 6

        DatasetSelectionTreeView {
            id: selectionTree

            Layout.fillWidth: true
            roleTitle: control.roleTitle
            selectionModel: control.selectionModel
            treeHeight: control.treeHeight
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 205
            spacing: 6

            QuiChart {
                Layout.fillWidth: true
                Layout.fillHeight: true
                animationDuration: 0
                chartType: "pie"
                chartData: statisticsModel.imageChartData
                chartOptions: ({
                    maintainAspectRatio: false,
                    legend: { display: false },
                    title: {
                        display: true,
                        text: qsTr("类别图像占比")
                    },
                    tooltips: {
                        mode: "index",
                        intersect: false
                    }
                })
            }

            QuiChart {
                Layout.fillWidth: true
                Layout.fillHeight: true
                animationDuration: 0
                chartType: "pie"
                chartData: statisticsModel.instanceChartData
                chartOptions: ({
                    maintainAspectRatio: false,
                    legend: { display: false },
                    title: {
                        display: true,
                        text: qsTr("类别实例占比")
                    },
                    tooltips: {
                        mode: "index",
                        intersect: false
                    }
                })
            }
        }
    }
}
