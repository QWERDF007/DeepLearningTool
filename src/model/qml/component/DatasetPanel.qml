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

    // Chart.js invokes tooltip callbacks with the active item and the complete
    // chart data object.  Keeping this presentation rule here lets QuiChart
    // remain independent from dataset-specific terminology while preserving
    // the detailed text prepared by DatasetSelectionStatisticsModel.
    function chartTooltipLabel(tooltipItem, data) {
        var datasets = data && data.datasets ? data.datasets : []
        var dataset = tooltipItem && tooltipItem.datasetIndex >= 0
                && tooltipItem.datasetIndex < datasets.length
                ? datasets[tooltipItem.datasetIndex]
                : null
        var tooltips = dataset && dataset.tooltips ? dataset.tooltips : []
        var index = tooltipItem ? Number(tooltipItem.index) : -1

        if (dataset && index >= 0 && index < tooltips.length && tooltips[index])
            return String(tooltips[index]).split("\n")

        var label = dataset && dataset.label ? String(dataset.label) + ": " : ""
        var value = tooltipItem && tooltipItem.value !== undefined
                ? tooltipItem.value
                : (tooltipItem && tooltipItem.yLabel !== undefined ? tooltipItem.yLabel : "")
        return label + value
    }

    function chartTooltipTitle() {
        // The model's tooltip text already contains the category name.  Leave
        // the Chart.js title row empty so it is not shown twice.
        return ""
    }

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
                        // A pie slice is an area, not a point on an index axis.
                        // Require the pointer to intersect the arc so the
                        // tooltip always describes the slice under the cursor.
                        mode: "nearest",
                        intersect: true,
                        callbacks: {
                            title: control.chartTooltipTitle,
                            label: control.chartTooltipLabel
                        }
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
                        // A pie slice is an area, not a point on an index axis.
                        // Require the pointer to intersect the arc so the
                        // tooltip always describes the slice under the cursor.
                        mode: "nearest",
                        intersect: true,
                        callbacks: {
                            title: control.chartTooltipTitle,
                            label: control.chartTooltipLabel
                        }
                    }
                })
            }
        }
    }
}
