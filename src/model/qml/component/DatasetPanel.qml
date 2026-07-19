import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import dltool.data
import dltool.model
import dltool.ui
import quickui

Item {
    id: control

    property DataManager dataManager: null
    property DataSelectionTreeModel selectionModel: null
    property string roleTitle: qsTr("数据集")

    DatasetSelectionStatisticsModel {
        id: statisticsModel
        dataManager: control.dataManager
        selectionModel: control.selectionModel
    }

    QuiSplitView {
        id: content

        anchors.fill: parent
        anchors.margins: 5
        orientation: Qt.Vertical

        DatasetSelectionTreeView {
            id: selectionTree

            SplitView.fillWidth: true
            SplitView.fillHeight: true
            roleTitle: control.roleTitle
            selectionModel: control.selectionModel
        }

        RowLayout {
            SplitView.fillWidth: true
            // SplitView.fillHeight: true
            SplitView.preferredHeight: 200
            spacing: 5

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
