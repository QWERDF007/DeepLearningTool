import QtQuick
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

EvaluationChartPanelBase {
    id: control
    title: qsTr("PR 曲线")

    property var precisionRecallClasses: []
    property int selectedPrecisionRecallClassId: -1

    function transformChartData(chartId, chartData) {
        var precisionRecallId = EvaluationProtocolKeys.chartIdPrecisionRecall
        if (chartId !== precisionRecallId && chartId !== "precision_recall")
            return chartData

        var sourceDatasets = chartData.datasets ? chartData.datasets : []
        var selectedClassId = Number(control.selectedPrecisionRecallClassId)
        var firstClassId = -1
        var firstClassDataset = null
        var displayedDatasets = []

        for (var index = 0; index < sourceDatasets.length; ++index) {
            var dataset = sourceDatasets[index]
            if (!dataset)
                continue
            var seriesKind = dataset.series_kind !== undefined ? String(dataset.series_kind) : ""
            if (seriesKind === "average" || seriesKind === EvaluationProtocolKeys.seriesKindAverage) {
                displayedDatasets.push(dataset)
            } else if (seriesKind === "class" || seriesKind === EvaluationProtocolKeys.seriesKindClass) {
                var classId = Number(dataset.class_id)
                if (firstClassId < 0) {
                    firstClassId = classId
                    firstClassDataset = dataset
                }
                if (classId === selectedClassId)
                    displayedDatasets.push(dataset)
            }
        }

        if (selectedClassId < 0 && firstClassId >= 0)
            displayedDatasets.push(firstClassDataset)

        chartData.datasets = displayedDatasets
        return chartData
    }

    function precisionRecallTicks() {
        return ({
            min: 0,
            max: 1,
            stepSize: 0.2,
            maxTicksLimit: 6,
            precision: 1,
            maxRotation: 0,
            minRotation: 0,
            fontColor: control.chartFontColor
        })
    }

    function precisionRecallTooltipLabel(tooltipItem, data) {
        var datasets = data && data.datasets ? data.datasets : []
        var datasetIndex = tooltipItem && tooltipItem.datasetIndex !== undefined
                ? Number(tooltipItem.datasetIndex) : -1
        var dataset = datasetIndex >= 0 && datasetIndex < datasets.length
                ? datasets[datasetIndex] : null
        var label = dataset && dataset.label !== undefined ? String(dataset.label) : ""
        if (label.length > 0)
            label += ": "

        var value = tooltipItem && tooltipItem.value !== undefined && tooltipItem.value !== null
                ? tooltipItem.value
                : (tooltipItem && tooltipItem.yLabel !== undefined ? tooltipItem.yLabel : "")
        return label + control.formatChartTooltipNumber(value)
    }

    function prepareMethodOptions(descriptor, options) {
        var chartId = descriptor && descriptor.chart_id ? String(descriptor.chart_id) : ""
        if (chartId === "precision_recall" || chartId === EvaluationProtocolKeys.chartIdPrecisionRecall) {
            options.tooltips = options.tooltips || ({})
            options.tooltips.callbacks = options.tooltips.callbacks || ({})
            options.tooltips.callbacks.label = control.precisionRecallTooltipLabel
            options.maintainAspectRatio = false
            options.responsive = true
            options.legend = options.legend || ({})
            options.legend.display = true
            options.legend.position = "top"
            options.scales = options.scales || ({})
            options.scales.xAxes = [{
                type: "linear",
                display: true,
                ticks: control.precisionRecallTicks(),
                scaleLabel: {
                    display: true,
                    labelString: "Recall",
                    fontColor: control.chartFontColor
                }
            }]
            options.scales.yAxes = [{
                type: "linear",
                display: true,
                ticks: control.precisionRecallTicks(),
                scaleLabel: {
                    display: true,
                    labelString: "Precision",
                    fontColor: control.chartFontColor
                }
            }]
        }
        return options
    }

    function refreshPrecisionRecallClasses() {
        var descriptor = control.displayDescriptor
        var datasets = descriptor && descriptor.data && descriptor.data.datasets
                ? descriptor.data.datasets : []
        var options = []
        var previousId = Number(control.selectedPrecisionRecallClassId)
        for (var index = 0; index < datasets.length; ++index) {
            var dataset = datasets[index]
            if (!dataset || (String(dataset.series_kind) !== "class" && String(dataset.series_kind) !== EvaluationProtocolKeys.seriesKindClass))
                continue
            options.push({
                classId: Number(dataset.class_id),
                name: dataset.class_name !== undefined ? String(dataset.class_name) : String(dataset.label),
                color: dataset.borderColor !== undefined ? String(dataset.borderColor) : "transparent"
            })
        }
        control.precisionRecallClasses = options
        var selected = -1
        for (var optionIndex = 0; optionIndex < options.length; ++optionIndex) {
            if (Number(options[optionIndex].classId) === previousId) {
                selected = previousId
                break
            }
        }
        if (selected < 0 && options.length > 0)
            selected = Number(options[0].classId)
        control.selectedPrecisionRecallClassId = selected
    }

    function refreshMethodSpecificState() {
        control.refreshPrecisionRecallClasses()
    }

    headerContent: EvaluationClassSelector {
        anchors.fill: parent
        visible: control.displayDescriptor
                 && (control.displayDescriptor.chart_id === "precision_recall"
                     || control.displayDescriptor.chart_id === EvaluationProtocolKeys.chartIdPrecisionRecall)
                 && control.precisionRecallClasses.length > 0
        classOptions: control.precisionRecallClasses
        currentClassId: control.selectedPrecisionRecallClassId
        onClassChanged: function(newClassId) {
            control.selectedPrecisionRecallClassId = newClassId
        }
    }
}
