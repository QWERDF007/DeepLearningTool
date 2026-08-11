import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

Rectangle {
    id: control
    color: QuiColor.Primary
    property ModelEvaluationViewModel evaluation: null
    property int chartRevision: 0
    readonly property string chartFontColor: QuiColor.FontPrimary.toString()

    function chartDataForDisplay(chartId, chartData) {
        if (!chartData || typeof chartData !== "object") {
            return ({ labels: [], datasets: [] })
        }

        try {
            return JSON.parse(JSON.stringify(chartData))
        } catch (error) {
            return ({ labels: [], datasets: [] })
        }
    }

    function chartModelCount() {
        var charts = control.evaluation ? control.evaluation.charts : null
        return charts ? charts.rowCount() : 0
    }

    /* 参考数据集仍属于 Chart.js 数据集，但图例只显示两个分布数据集。 */
    function anomalyLegendFilter(item, data) {
        var datasetIndex = item && item.datasetIndex !== undefined ? Number(item.datasetIndex) : -1
        var datasets = data && data.datasets ? data.datasets : []
        var dataset = datasetIndex >= 0 && datasetIndex < datasets.length ? datasets[datasetIndex] : null
        var seriesKind = dataset && dataset.series_kind !== undefined ? String(dataset.series_kind) : ""
        return seriesKind === "good" || seriesKind === "anomaly"
    }

    function anomalyTooltipLabel(tooltipItem, data) {
        var datasets = data && data.datasets ? data.datasets : []
        var datasetIndex = tooltipItem && tooltipItem.datasetIndex !== undefined
                ? Number(tooltipItem.datasetIndex) : -1
        var dataset = datasetIndex >= 0 && datasetIndex < datasets.length
                ? datasets[datasetIndex] : null
        if (dataset && dataset.tooltipXOnly)
            return null

        var label = dataset && dataset.label !== undefined ? String(dataset.label) : ""
        if (label.length > 0)
            label += ": "
        if (tooltipItem && tooltipItem.value !== undefined && tooltipItem.value !== null)
            label += tooltipItem.value
        else if (tooltipItem && tooltipItem.yLabel !== undefined)
            label += tooltipItem.yLabel
        return label
    }

    function chartOptionsForDescriptor(descriptor) {
        var options = ChartPresenter.prepareOptions(descriptor.options, control.chartFontColor)
        try {
            options = JSON.parse(JSON.stringify(options))
        } catch (error) {
            options = ({})
        }
        if (descriptor.chart_id === "anomaly_score_distribution"
                && options.legend && options.legend.labels)
            options.legend.labels.filter = control.anomalyLegendFilter
        if (descriptor.chart_id === "anomaly_score_distribution") {
            if (!options.tooltips)
                options.tooltips = ({})
            if (!options.tooltips.callbacks)
                options.tooltips.callbacks = ({})
            options.tooltips.callbacks.label = control.anomalyTooltipLabel
        }
        return options
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            QuiText { 
                text: qsTr("方法图表")
                font: QuiFont.Subtitle 
            }
            QuiComboBox {
                id: chartSelector
                Layout.fillWidth: true
                visible: count > 1
                model: control.evaluation ? control.evaluation.charts : null
                textRole: "title"
                currentIndex: 0
                onCountChanged: {
                    if (count > 0 && (currentIndex < 0 || currentIndex >= count))
                        currentIndex = 0
                }
            }
        }
        QuiChart {
            id: chart
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 1
            Layout.minimumHeight: 1
            /* 模型重置期间保留 Canvas 的布局，由 QuiChart 绘制空状态。 */
            visible: true
            property var descriptor: {
                /* QAbstractItemModel 重置期间仍保持图表可用，当前索引可能暂时为 -1。 */
                var revision = control.chartRevision
                var charts = control.evaluation ? control.evaluation.charts : null
                var count = control.chartModelCount()
                var index = chartSelector.currentIndex
                if (index < 0 || index >= count)
                    index = 0
                return charts && count > 0 ? charts.descriptor(index) : ({})
            }
            chartType: descriptor.kind || "line"
            chartData: control.chartDataForDisplay(
                           descriptor.chart_id, ChartPresenter.prepareData(descriptor.data))
            chartOptions: control.chartOptionsForDescriptor(descriptor)
        }
        Connections {
            target: control.evaluation ? control.evaluation.charts : null
            function refreshChartModel() {
                if (chartSelector.count > 0
                        && (chartSelector.currentIndex < 0 || chartSelector.currentIndex >= chartSelector.count))
                    chartSelector.currentIndex = 0
                control.chartRevision += 1
            }
            function onModelReset() {
                chartSelector.currentIndex = 0
                refreshChartModel()
            }
            function onRowsInserted(parent, first, last) {
                refreshChartModel()
            }
            function onRowsRemoved(parent, first, last) {
                refreshChartModel()
            }
            function onDataChanged(topLeft, bottomRight, roles) {
                refreshChartModel()
            }
        }
        QuiText {
            visible: {
                var revision = control.chartRevision
                return control.chartModelCount() === 0
            }
            text: qsTr("当前方法没有可用图表")
            color: QuiColor.FontDark
        }
    }
}
