import QtQuick
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

EvaluationChartPanelBase {
    id: control
    title: qsTr("分数分布图")

    property bool thresholdDragActive: false
    property real dragClassificationThreshold: 0.5
    property real classificationThreshold: 0.5

    readonly property var evaluationGroup: control.findEvaluationGroup()
    readonly property real displayedClassificationThreshold: control.thresholdDragActive
                                                       ? control.dragClassificationThreshold
                                                       : control.classificationThreshold

    function findEvaluationGroup() {
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
            if (group && String(group.nameEn).toLowerCase() === "evaluation")
                return group
        }
        return null
    }

    function readClassificationThreshold() {
        var group = control.evaluationGroup
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

    function scoreScale(revision) {
        // Keep the geometry revision as an explicit binding dependency. The
        // Chart.js scale object is mutable and does not notify QML itself.
        var geometryRevision = revision
        var instance = control.chart.chartInstance
        if (!instance || !instance.scales)
            return null
        return instance.scales[EvaluationProtocolKeys.chartAxisScore] || null
    }

    function scoreChartArea(revision) {
        var geometryRevision = revision
        var instance = control.chart.chartInstance
        return instance && instance.chartArea ? instance.chartArea : null
    }

    function referenceLineGeometry(revision) {
        var instance = control.chart.chartInstance
        var scales = instance && instance.scales ? instance.scales : null
        var countScale = scales ? scales[EvaluationProtocolKeys.chartAxisCount] : null
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

    function scorePixelForValue(value, revision) {
        var scale = control.scoreScale(revision)
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
        var area = control.scoreChartArea(control.chartGeometryRevision)
        if (!area)
            return
        var x = Math.max(Number(area.left), Math.min(Number(area.right), Number(pixel)))
        var value = control.valueForScorePixel(x)
        if (isFinite(value) && Math.abs(value - control.dragClassificationThreshold) > 1e-9) {
            control.dragClassificationThreshold = value
        }
    }

    function commitDraggedThreshold() {
        var group = control.evaluationGroup
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

    function anomalyLegendFilter(item, data) {
        var datasetIndex = item && item.datasetIndex !== undefined ? Number(item.datasetIndex) : -1
        var datasets = data && data.datasets ? data.datasets : []
        var dataset = datasetIndex >= 0 && datasetIndex < datasets.length ? datasets[datasetIndex] : null
        var seriesKind = dataset && dataset.series_kind !== undefined ? String(dataset.series_kind) : ""
        return seriesKind === EvaluationProtocolKeys.seriesKindGood
                || seriesKind === EvaluationProtocolKeys.seriesKindAnomaly
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

    function prepareMethodOptions(descriptor, options) {
        var anomalyChartId = EvaluationProtocolKeys.chartIdAnomalyScoreDistribution
        if (descriptor.chart_id === anomalyChartId && options.legend && options.legend.labels)
            options.legend.labels.filter = control.anomalyLegendFilter
        if (descriptor.chart_id === anomalyChartId) {
            options.tooltips.callbacks.label = control.anomalyTooltipLabel
        }
        return options
    }

    function refreshMethodSpecificState() {
        control.syncClassificationThreshold()
    }

    onTestParamsChanged: {
        control.syncClassificationThreshold()
        control.scheduleClassificationThresholdSync()
    }

    chartOverlay: Item {
        anchors.fill: parent

        Rectangle {
            id: classificationThresholdLine
            objectName: "classificationThresholdLine"
            z: 2
            width: 2
            x: control.scorePixelForValue(control.displayedClassificationThreshold,
                                          control.chartGeometryRevision) - width / 2
            y: {
                var area = control.scoreChartArea(control.chartGeometryRevision)
                return area ? Number(area.top) : 0
            }
            height: {
                var area = control.scoreChartArea(control.chartGeometryRevision)
                return area ? Math.max(0, Number(area.bottom) - Number(area.top)) : 0
            }
            color: "#1E88E5"
            visible: control.chart.chartReady
                     && control.displayDescriptor
                     && control.displayDescriptor.chart_id
                        === EvaluationProtocolKeys.chartIdAnomalyScoreDistribution
                     && x + width >= 0 && x <= control.chartSurfaceItem.width
        }

        QuiText {
            id: classificationThresholdLabel
            z: 4
            width: Math.max(56, implicitWidth + 4)
            height: implicitHeight
            x: {
                var lineX = control.scorePixelForValue(control.displayedClassificationThreshold,
                                                        control.chartGeometryRevision)
                var maxX = Math.max(0, control.chartSurfaceItem.width - width)
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
                var point = classificationThresholdHandle.mapToItem(control.chartSurfaceItem, mouse.x, mouse.y)
                control.updateDraggedThreshold(point.x)
            }
            onPositionChanged: function(mouse) {
                if (pressed) {
                    var point = classificationThresholdHandle.mapToItem(control.chartSurfaceItem, mouse.x, mouse.y)
                    control.updateDraggedThreshold(point.x)
                }
            }
            onReleased: function(mouse) {
                var point = classificationThresholdHandle.mapToItem(control.chartSurfaceItem, mouse.x, mouse.y)
                control.updateDraggedThreshold(point.x)
                control.commitDraggedThreshold()
            }
        }
    }

    Connections {
        target: control.evaluationGroup
        function onValueChanged(name, value) {
            if (String(name).toLowerCase() !== "classification_threshold")
                return
            if (isFinite(Number(value)))
                control.classificationThreshold = Number(value)
            if (!control.thresholdDragActive && isFinite(Number(value)))
                control.dragClassificationThreshold = Number(value)
        }
    }
}
