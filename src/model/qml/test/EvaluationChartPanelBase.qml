import QtQuick
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

/*
 * 方法图表面板 Base：
 *
 * - 收拢所有 Chart.js 边界转换逻辑（nativeChartData/options 深拷贝、
 *   tooltip title、图表模型刷新与 revision 管理）。
 * - 方法特有逻辑通过 JS 钩子扩展：
 *   - transformChartData(descriptor, chartData)：数据集裁剪/过滤。
 *   - prepareMethodOptions(descriptor, options)：方法 options/回调。
 *   - refreshMethodSpecificState()：图表数据变化后刷新方法状态。
 * - 头部与图表 overlay 通过 headerContent/chartOverlay 注入。
 */
Rectangle {
    id: control
    color: QuiColor.Primary

    property ModelEvaluationViewModel evaluation: null
    property ITestParams testParams: null
    property string title: qsTr("方法图表")

    property int chartRevision: 0
    property int chartGeometryRevision: 0
    readonly property string chartFontColor: QuiColor.FontPrimary.toString()

    readonly property var displayDescriptor: control.chartDescriptorForDisplay(control.chartRevision)

    property alias chart: chart
    property alias chartSurfaceItem: chartSurface
    property alias headerContent: headerAction.data
    property alias chartOverlay: chartOverlayHost.data

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

        var converted = control.nativeChartData(chartData)
        return control.transformChartData(chartId, converted)
    }

    function transformChartData(chartId, chartData) {
        return chartData
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

    function prepareMethodOptions(descriptor, options) {
        return options
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
        return control.prepareMethodOptions(descriptor, options)
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
            if (descriptor.chart_id === EvaluationProtocolKeys.chartIdPrecisionRecall)
                return descriptor
            if (descriptor.chart_id === EvaluationProtocolKeys.chartIdAnomalyScoreDistribution)
                anomalyDescriptor = descriptor
        }
        /* 旧结果可能仍保存按类别指标，但该图不再属于方法图表区域。 */
        return anomalyDescriptor
    }

    function hasDisplayChart(revision) {
        var descriptor = control.chartDescriptorForDisplay()
        return descriptor && descriptor.chart_id !== undefined && descriptor.chart_id !== ""
    }

    function refreshMethodSpecificState() {
    }

    Component.onCompleted: Qt.callLater(control.refreshMethodSpecificState)
    onEvaluationChanged: {
        control.chartRevision += 1
        Qt.callLater(control.refreshMethodSpecificState)
    }
    onChartRevisionChanged: Qt.callLater(control.refreshMethodSpecificState)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5

        // 顶栏 Header 容器（包含标题与右上角动作插槽）
        RowLayout {
            id: headerHost
            Layout.fillWidth: true
            Layout.preferredHeight: 32

            QuiText {
                text: control.title
                font: QuiFont.Subtitle
            }

            Item { Layout.fillWidth: true }

            Item {
                id: headerAction
                visible: children.length > 0
                Layout.preferredWidth: visible ? 220 : 0
                Layout.preferredHeight: visible ? 32 : 0
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
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

                chartType: descriptor && descriptor.kind ? String(descriptor.kind) : "line"

                chartData: {
                    var prepared = ChartPresenter.prepareData(descriptor.data)
                    return control.chartDataForDisplay(descriptor.chart_id, prepared)
                }
                chartOptions: {
                    return control.chartOptionsForDescriptor(descriptor)
                }
                function scheduleGeometryRefresh() {
                    control.chartGeometryRevision += 1
                    // Chart.js recomputes chartArea during the resize/update
                    // cycle. Re-read it once the current QML geometry pass has
                    // completed so overlays do not retain the old bounds.
                    Qt.callLater(function() {
                        control.chartGeometryRevision += 1
                    })
                }

                onChartCreated: scheduleGeometryRefresh()
                onChartUpdated: scheduleGeometryRefresh()
                onWidthChanged: scheduleGeometryRefresh()
                onHeightChanged: scheduleGeometryRefresh()
            }

            Item {
                id: chartOverlayHost
                anchors.fill: parent
            }
        }

        Connections {
            target: control.evaluation ? control.evaluation.charts : null
            function refreshChartModel() {
                control.chartRevision += 1
                Qt.callLater(control.refreshMethodSpecificState)
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
            visible: !control.hasDisplayChart(control.chartRevision)
            text: qsTr("当前方法没有可用图表")
            color: QuiColor.FontDark
        }
    }
}
