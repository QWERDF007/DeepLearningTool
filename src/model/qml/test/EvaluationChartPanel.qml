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

    function chartModelCount() {
        var charts = control.evaluation ? control.evaluation.charts : null
        return charts ? charts.rowCount() : 0
    }

    // Evaluation descriptors expose nested QVariantMap/QVariantList values.
    // Clone the data at the QML boundary so Chart.js receives native
    // JavaScript objects, arrays, and strings (including dataset colors).
    function chartDataForDisplay(chartData) {
        if (!chartData || typeof chartData !== "object")
            return ({labels: [], datasets: []})

        try {
            return JSON.parse(JSON.stringify(chartData))
        } catch (error) {
            // Keep the chart usable if a future descriptor adds a value that
            // cannot be serialized.
            return ({labels: [], datasets: []})
        }
    }

    // Tooltip and interaction settings are nested QVariantMap values too.
    // Convert them before Chart.js reads mode/intersect and related options.
    function chartOptionsForDisplay(options) {
        if (!options || typeof options !== "object")
            return ({maintainAspectRatio: false})

        try {
            return JSON.parse(JSON.stringify(options))
        } catch (error) {
            return ({maintainAspectRatio: false})
        }
    }

    function copyMap(source) {
        var result = ({})
        if (!source || typeof source !== "object")
            return result

        for (var key in source)
            result[key] = source[key]
        return result
    }

    function axisWithFontColor(axis) {
        var axisCopy = copyMap(axis)
        var ticksCopy = copyMap(axisCopy.ticks)
        var scaleLabelCopy = copyMap(axisCopy.scaleLabel)
        ticksCopy.fontColor = control.chartFontColor
        scaleLabelCopy.fontColor = control.chartFontColor
        axisCopy.ticks = ticksCopy
        axisCopy.scaleLabel = scaleLabelCopy
        return axisCopy
    }

    function chartOptionsForDescriptor(descriptor) {
        var options = control.chartOptionsForDisplay(descriptor.options)
        if (descriptor.chart_id !== "anomaly_score_distribution")
            return options

        var filtered = ({})
        for (var key in options)
            filtered[key] = options[key]

        var legend = filtered.legend || ({})
        var legendCopy = ({})
        for (var legendKey in legend)
            legendCopy[legendKey] = legend[legendKey]

        var labels = legendCopy.labels || ({})
        var labelsCopy = ({})
        for (var labelKey in labels)
            labelsCopy[labelKey] = labels[labelKey]
        labelsCopy.fontColor = control.chartFontColor
        labelsCopy.filter = function(item, data) {
            // Threshold markers remain line datasets so they can render as
            // vertical dashed lines, but only the two distributions belong
            // in the legend.  Chart.js normally provides item.text; the
            // dataset fallback also covers updates from QVariant-backed data.
            var itemLabel = item && item.text !== undefined ? String(item.text) : ""
            if (itemLabel.length > 0)
                return itemLabel === "GOOD" || itemLabel === "Anomaly"

            var datasetIndex = item && item.datasetIndex !== undefined ? Number(item.datasetIndex) : -1
            var datasets = data && data.datasets ? data.datasets : []
            var dataset = datasetIndex >= 0 && datasetIndex < datasets.length ? datasets[datasetIndex] : null
            var datasetLabel = dataset && dataset.label !== undefined ? String(dataset.label) : ""
            return datasetLabel === "GOOD" || datasetLabel === "Anomaly"
        }
        legendCopy.labels = labelsCopy
        filtered.legend = legendCopy

        var scales = copyMap(filtered.scales)
        var xAxes = scales.xAxes || []
        var yAxes = scales.yAxes || []
        var xAxesCopy = []
        var yAxesCopy = []
        for (var xIndex = 0; xIndex < xAxes.length; ++xIndex)
            xAxesCopy.push(axisWithFontColor(xAxes[xIndex]))
        for (var yIndex = 0; yIndex < yAxes.length; ++yIndex)
            yAxesCopy.push(axisWithFontColor(yAxes[yIndex]))
        scales.xAxes = xAxesCopy
        scales.yAxes = yAxesCopy
        filtered.scales = scales
        return filtered
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            QuiText { text: qsTr("方法图表"); font: QuiFont.Title }
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
            // Keep the Canvas in the layout while the model is resetting.
            // QuiChart renders its own empty state when chartData is empty.
            visible: true
            property var descriptor: {
                // Keep the chart usable while QAbstractItemModel is resetting.
                // The model can temporarily expose currentIndex == -1.
                var revision = control.chartRevision
                var charts = control.evaluation ? control.evaluation.charts : null
                var count = control.chartModelCount()
                var index = chartSelector.currentIndex
                if (index < 0 || index >= count)
                    index = 0
                return charts && count > 0 ? charts.descriptor(index) : ({})
            }
            chartType: descriptor.kind || "line"
            chartData: control.chartDataForDisplay(descriptor.data)
            chartOptions: control.chartOptionsForDescriptor(descriptor)
        }
        Connections {
            target: control.evaluation ? control.evaluation.charts : null
            function onModelReset() {
                chartSelector.currentIndex = 0
                control.chartRevision += 1
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
