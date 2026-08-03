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

    function chartModelCount() {
        var charts = control.evaluation ? control.evaluation.charts : null
        return charts ? charts.rowCount() : 0
    }

    function chartOptionsForDescriptor(descriptor) {
        var options = descriptor.options || ({maintainAspectRatio: false})
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
        labelsCopy.filter = function(item) {
            return item && (item.text === "GOOD" || item.text === "Anomaly")
        }
        legendCopy.labels = labelsCopy
        filtered.legend = legendCopy
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
            chartData: descriptor.data || ({labels: [], datasets: []})
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
