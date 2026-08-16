import QtQuick
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

Rectangle {
    id: control
    color: QuiColor.Primary
    property ModelEvaluationViewModel evaluation: null
    property ITestParams testParams: null
    property int chartRevision: 0
    property int chartGeometryRevision: 0
    property bool thresholdDragActive: false
    property real dragClassificationThreshold: 0.5
    property real classificationThreshold: 0.5
    readonly property string chartFontColor: QuiColor.FontPrimary.toString()
    property var precisionRecallClasses: []
    property int selectedPrecisionRecallClassId: -1
    readonly property var inferenceGroup: control.findInferenceGroup()
    readonly property real displayedClassificationThreshold: control.thresholdDragActive
                                                       ? control.dragClassificationThreshold
                                                       : control.classificationThreshold
    readonly property var displayDescriptor: control.chartDescriptorForDisplay(control.chartRevision)

    function findInferenceGroup() {
        var params = control.testParams
        if (!params)
            return null
        var count = Number(params.count)
        if (!isFinite(count) && params.rowCount)
            count = Number(params.rowCount())
        if (!isFinite(count) || count < 1)
            return null
        for (var index = 0; index < count; ++index) {
            var group = params.groupAt(index)
            if (group && String(group.nameEn).toLowerCase() === "inference")
                return group
        }
        return null
    }

    function readClassificationThreshold() {
        var group = control.inferenceGroup
        if (group) {
            var value = Number(group.valueForName("classification_threshold"))
            if (isFinite(value))
                return value
        }
        if (control.evaluation && control.evaluation.anomalyDetection
                && isFinite(Number(control.evaluation.confidenceThreshold)))
            return Number(control.evaluation.confidenceThreshold)
        return 0.5
    }

    function syncClassificationThreshold() {
        var value = Number(control.readClassificationThreshold())
        if (!isFinite(value))
            return
        control.classificationThreshold = value
        if (!control.thresholdDragActive)
            control.dragClassificationThreshold = value
    }

    function scheduleClassificationThresholdSync() {
        Qt.callLater(function() {
            control.syncClassificationThreshold()
        })
    }

    function scoreScale() {
        var revision = control.chartGeometryRevision
        var instance = chart.chartInstance
        if (!instance || !instance.scales)
            return null
        return instance.scales["score-axis"] || null
    }

    function scoreChartArea() {
        var revision = control.chartGeometryRevision
        var instance = chart.chartInstance
        return instance && instance.chartArea ? instance.chartArea : null
    }

    function referenceLineGeometry(revision) {
        var instance = chart.chartInstance
        var scales = instance && instance.scales ? instance.scales : null
        var countScale = scales ? scales["count-axis"] : null
        if (!instance || !countScale)
            return null

        var maxValue = 0
        var datasets = instance.data && instance.data.datasets ? instance.data.datasets : []
        for (var datasetIndex = 0; datasetIndex < datasets.length; ++datasetIndex) {
            var dataset = datasets[datasetIndex]
            if (!dataset || !dataset.tooltipXOnly || !dataset.data)
                continue
            for (var pointIndex = 0; pointIndex < dataset.data.length; ++pointIndex) {
                var point = dataset.data[pointIndex]
                var pointValue = point && typeof point === "object" ? Number(point.y) : NaN
                if (isFinite(pointValue))
                    maxValue = Math.max(maxValue, pointValue)
            }
        }
        if (!(maxValue > 0))
            return null

        try {
            var zeroPixel = Number(countScale.getPixelForValue(0))
            var maxPixel = Number(countScale.getPixelForValue(maxValue))
            if (isFinite(zeroPixel) && isFinite(maxPixel))
                return ({ top: Math.min(zeroPixel, maxPixel), bottom: Math.max(zeroPixel, maxPixel) })
        } catch (error) {
        }
        return null
    }

    function scorePixelForValue(value) {
        var scale = control.scoreScale()
        if (!scale || !isFinite(Number(value)))
            return -100
        try {
            if (scale.getPixelForValue)
                return Number(scale.getPixelForValue(Number(value)))
            var min = Number(scale.min)
            var max = Number(scale.max)
            if (isFinite(min) && isFinite(max) && max > min)
                return Number(scale.left) + (Number(value) - min) * (Number(scale.right - scale.left)) / (max - min)
        } catch (error) {
        }
        return -100
    }

    function valueForScorePixel(pixel) {
        var scale = control.scoreScale()
        if (!scale || !isFinite(Number(pixel)))
            return NaN
        try {
            if (scale.getValueForPixel)
                return Number(scale.getValueForPixel(Number(pixel)))
            var min = Number(scale.min)
            var max = Number(scale.max)
            if (isFinite(min) && isFinite(max) && max > min)
                return min + (Number(pixel) - Number(scale.left)) * (max - min)
                         / Number(scale.right - scale.left)
        } catch (error) {
        }
        return NaN
    }

    function updateDraggedThreshold(pixel) {
        var area = control.scoreChartArea()
        if (!area)
            return
        var x = Math.max(Number(area.left), Math.min(Number(area.right), Number(pixel)))
        var value = control.valueForScorePixel(x)
        if (isFinite(value) && Math.abs(value - control.dragClassificationThreshold) > 1e-9) {
            control.dragClassificationThreshold = value
        }
    }

    function commitDraggedThreshold() {
        var group = control.inferenceGroup
        var value = Number(control.dragClassificationThreshold)
        if (!group || !isFinite(value))
        {
            control.thresholdDragActive = false
            return
        }

        /* 先提交参数，避免拖动状态结束时绑定立即读回旧阈值。 */
        var accepted = group.setValueForName("classification_threshold", value)
        var appliedValue = Number(group.valueForName("classification_threshold"))
        if (accepted) {
            if (isFinite(appliedValue)) {
                control.classificationThreshold = appliedValue
                control.dragClassificationThreshold = appliedValue
            }
        }
        control.thresholdDragActive = false
    }

    function nativeChartData(chartData) {
        if (!chartData || typeof chartData !== "object")
            return ({ labels: [], datasets: [] })

        /* QVariantMap/QVariantList 先转换为 Chart.js 可直接遍历的 JS 对象。 */
        try {
            var serialized = JSON.stringify(chartData)
            if (serialized) {
                var converted = JSON.parse(serialized)
                if (converted && typeof converted === "object") {
                    return converted
                }
            }
        } catch (error) {
            // 保留原始 QVariant 对象，交由图表组件继续处理。
        }
        return chartData
    }

    function chartDataForDisplay(chartId, chartData) {
        if (!chartData || typeof chartData !== "object")
            return ({ labels: [], datasets: [] })

        if (chartId !== "precision_recall") {
            return control.nativeChartData(chartData)
        }

        var converted = control.nativeChartData(chartData)
        var sourceDatasets = converted.datasets ? converted.datasets : []
        var selectedClassId = Number(control.selectedPrecisionRecallClassId)
        var firstClassId = -1
        var firstClassDataset = null
        var displayedDatasets = []
        for (var index = 0; index < sourceDatasets.length; ++index) {
            var dataset = sourceDatasets[index]
            if (!dataset)
                continue
            var seriesKind = dataset.series_kind !== undefined ? String(dataset.series_kind) : ""
            if (seriesKind === "average") {
                displayedDatasets.push(dataset)
            } else if (seriesKind === "class") {
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

        converted.datasets = displayedDatasets
        return converted
    }

    function refreshPrecisionRecallClasses() {
        var descriptor = control.chartDescriptorForDisplay(control.chartRevision)
        var datasets = descriptor && descriptor.data && descriptor.data.datasets
                ? descriptor.data.datasets : []
        var options = []
        var previousId = Number(control.selectedPrecisionRecallClassId)
        for (var index = 0; index < datasets.length; ++index) {
            var dataset = datasets[index]
            if (!dataset || String(dataset.series_kind) !== "class")
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

    function chartModelCount() {
        var charts = control.evaluation ? control.evaluation.charts : null
        return charts ? charts.rowCount() : 0
    }

    function chartDescriptorForDisplay(revision) {
        var charts = control.evaluation ? control.evaluation.charts : null
        var count = control.chartModelCount()
        if (!charts || count <= 0)
            return ({})

        var anomalyDescriptor = ({})
        for (var index = 0; index < count; ++index) {
            var descriptor = charts.descriptor(index)
            if (descriptor.chart_id === "precision_recall")
                return descriptor
            if (descriptor.chart_id === "anomaly_score_distribution")
                anomalyDescriptor = descriptor
        }
        /* 旧结果可能仍保存按类别指标，但该图不再属于方法图表区域。 */
        return anomalyDescriptor
    }

    function hasDisplayChart(revision) {
        var descriptor = control.chartDescriptorForDisplay()
        return descriptor && descriptor.chart_id !== undefined && descriptor.chart_id !== ""
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

    function formatChartTooltipNumber(value) {
        if (value === undefined || value === null || value === "")
            return ""

        var number = Number(value)
        return isFinite(number) ? number.toFixed(4) : String(value)
    }

    function chartTooltipTitle(tooltipItems, data) {
        if (!tooltipItems || tooltipItems.length === 0)
            return ""

        var item = tooltipItems[0]
        var value = item.xLabel !== undefined && item.xLabel !== null && item.xLabel !== ""
                ? item.xLabel
                : item.label
        return control.formatChartTooltipNumber(value)
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
            label += control.formatChartTooltipNumber(tooltipItem.value)
        else if (tooltipItem && tooltipItem.yLabel !== undefined)
            label += control.formatChartTooltipNumber(tooltipItem.yLabel)
        return label
    }

    function chartOptionsForDescriptor(descriptor) {
        var options = ChartPresenter.prepareOptions(descriptor.options, control.chartFontColor)
        try {
            options = JSON.parse(JSON.stringify(options))
        } catch (error) {
            options = ({})
        }
        if (!options.tooltips)
            options.tooltips = ({})
        if (!options.tooltips.callbacks)
            options.tooltips.callbacks = ({})
        options.tooltips.callbacks.title = control.chartTooltipTitle
        if (descriptor.chart_id === "anomaly_score_distribution"
                && options.legend && options.legend.labels)
            options.legend.labels.filter = control.anomalyLegendFilter
        if (descriptor.chart_id === "anomaly_score_distribution") {
            options.tooltips.callbacks.label = control.anomalyTooltipLabel
        }
        if (descriptor.chart_id === "precision_recall") {
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

    Component.onCompleted: {
        control.syncClassificationThreshold()
        control.scheduleClassificationThresholdSync()
        Qt.callLater(control.refreshPrecisionRecallClasses)
    }
    onEvaluationChanged: {
        control.syncClassificationThreshold()
        control.scheduleClassificationThresholdSync()
        control.chartRevision += 1
        Qt.callLater(control.refreshPrecisionRecallClasses)
    }
    onChartRevisionChanged: Qt.callLater(control.refreshPrecisionRecallClasses)

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            QuiText { 
                text: qsTr("方法图表")
                font: QuiFont.Subtitle 
            }
            Item { Layout.fillWidth: true }
            EvaluationClassSelector {
                Layout.preferredWidth: 180
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                visible: control.displayDescriptor
                         && control.displayDescriptor.chart_id === "precision_recall"
                         && control.precisionRecallClasses.length > 0
                classOptions: control.precisionRecallClasses
                currentClassId: control.selectedPrecisionRecallClassId
                onClassChanged: function(newClassId) {
                    control.selectedPrecisionRecallClassId = newClassId
                }
            }
        }
        Item {
            id: chartSurface
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 1
            Layout.minimumHeight: 1
            visible: control.hasDisplayChart(control.chartRevision)

            QuiChart {
                id: chart
                anchors.fill: parent
                /* 模型重置期间保留 Canvas 的布局，由 QuiChart 绘制空状态。 */
                property var descriptor: {
                    return control.chartDescriptorForDisplay(control.chartRevision)
                }
                chartType: descriptor.kind || "line"
                chartData: {
                    var prepared = ChartPresenter.prepareData(descriptor.data)
                    return control.chartDataForDisplay(descriptor.chart_id, prepared)
                }
                chartOptions: {
                    return control.chartOptionsForDescriptor(descriptor, control.chartRevision)
                }
                onChartCreated: {
                    control.chartGeometryRevision += 1
                    control.scheduleClassificationThresholdSync()
                }
                onChartUpdated: control.chartGeometryRevision += 1
            }

            Rectangle {
                id: classificationThresholdLine
                z: 2
                width: 2
                x: control.scorePixelForValue(control.displayedClassificationThreshold) - width / 2
                y: {
                    var geometry = control.referenceLineGeometry(control.chartGeometryRevision)
                    var area = control.scoreChartArea()
                    return geometry ? Number(geometry.top) : (area ? Number(area.top) : 0)
                }
                height: {
                    var geometry = control.referenceLineGeometry(control.chartGeometryRevision)
                    var area = control.scoreChartArea()
                    return geometry ? Math.max(0, Number(geometry.bottom) - Number(geometry.top))
                                     : (area ? Math.max(0, Number(area.bottom) - Number(area.top)) : 0)
                }
                color: "#1E88E5"
                visible: chart.chartReady
                         && control.displayDescriptor
                         && control.displayDescriptor.chart_id === "anomaly_score_distribution"
                         && x + width >= 0 && x <= chartSurface.width
            }

            QuiText {
                id: classificationThresholdLabel
                z: 4
                width: Math.max(56, implicitWidth + 4)
                height: implicitHeight
                x: {
                    var lineX = control.scorePixelForValue(control.displayedClassificationThreshold)
                    var maxX = Math.max(0, chartSurface.width - width)
                    return Math.max(0, Math.min(maxX, lineX - width / 2))
                }
                y: Math.max(0, classificationThresholdLine.y - height - 2)
                text: isFinite(Number(control.displayedClassificationThreshold))
                      ? Number(control.displayedClassificationThreshold).toFixed(4) : ""
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                visible: classificationThresholdLine.visible
            }

            MouseArea {
                id: classificationThresholdHandle
                z: 3
                width: 16
                x: classificationThresholdLine.x - (width - classificationThresholdLine.width) / 2
                y: classificationThresholdLine.y
                height: classificationThresholdLine.height
                visible: classificationThresholdLine.visible
                enabled: visible
                hoverEnabled: true
                cursorShape: Qt.SizeHorCursor

                onPressed: function(mouse) {
                    control.thresholdDragActive = true
                    control.dragClassificationThreshold = control.classificationThreshold
                    var point = classificationThresholdHandle.mapToItem(chartSurface, mouse.x, mouse.y)
                    control.updateDraggedThreshold(point.x)
                }
                onPositionChanged: function(mouse) {
                    if (pressed) {
                        var point = classificationThresholdHandle.mapToItem(chartSurface, mouse.x, mouse.y)
                        control.updateDraggedThreshold(point.x)
                    }
                }
                onReleased: function(mouse) {
                    var point = classificationThresholdHandle.mapToItem(chartSurface, mouse.x, mouse.y)
                    control.updateDraggedThreshold(point.x)
                    control.commitDraggedThreshold()
                }
            }
        }
        Connections {
            target: control.evaluation ? control.evaluation.charts : null
            function refreshChartModel() {
                control.chartRevision += 1
                Qt.callLater(control.refreshPrecisionRecallClasses)
            }
            function onModelReset() {
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
                return !control.hasDisplayChart(control.chartRevision)
            }
            text: qsTr("当前方法没有可用图表")
            color: QuiColor.FontDark
        }
    }

    Connections {
        target: control.inferenceGroup
        function onValueChanged(name, value) {
            if (String(name).toLowerCase() !== "classification_threshold")
                return
            if (isFinite(Number(value)))
                control.classificationThreshold = Number(value)
            if (!control.thresholdDragActive && isFinite(Number(value)))
                control.dragClassificationThreshold = Number(value)
        }
    }

    onTestParamsChanged: {
        control.syncClassificationThreshold()
        control.scheduleClassificationThresholdSync()
    }
}
