import QtQuick
import QtTest

import dltool.model

TestCase {
    name: "EvaluationPanelSmoke"

    function test_componentsLoad_data() {
        return [
            { tag: "AnomalyMetrics", component: anomalyMetricsComponent },
            { tag: "DetectionMetrics", component: detectionMetricsComponent },
            { tag: "SegmentationImageMetrics", component: segmentationImageMetricsComponent },
            { tag: "AnomalyChart", component: anomalyChartComponent },
            { tag: "DetectionChart", component: detectionChartComponent },
            { tag: "DetectionMatrix", component: detectionMatrixComponent },
            { tag: "AnomalyMatrix", component: anomalyMatrixComponent },
            { tag: "DetectionGrid", component: detectionGridComponent },
            { tag: "AnomalyDetails", component: anomalyDetailsComponent },
            { tag: "ParamPanel", component: paramPanelComponent },
            { tag: "TrainParamsForm", component: trainParamsFormComponent },
            { tag: "TestTaskPanel", component: testTaskPanelComponent },
            { tag: "TestPanel", component: testPanelComponent }
        ]
    }

    function test_componentsLoad(data) {
        var object = createTemporaryObject(data.component, null)
        verify(object, data.tag)

        if (data.tag === "ParamPanel" || data.tag === "TrainParamsForm")
            compare(object.params, null)
        if (data.tag === "TestTaskPanel")
            compare(object.taskManager, null)

        if (data.tag !== "AnomalyChart")
            return

        var source = {
            labels: ["Good"],
            datasets: [{ data: [1], nested: { marker: "source" } }]
        }
        var converted = object.nativeChartData(source)
        verify(converted !== source)
        converted.labels[0] = "changed"
        converted.datasets[0].data[0] = 99
        converted.datasets[0].nested.marker = "changed"
        compare(source.labels[0], "Good")
        compare(source.datasets[0].data[0], 1)
        compare(source.datasets[0].nested.marker, "source")

        var descriptor = {
            options: {
                legend: { labels: { display: true } },
                scales: { xAxes: [{ ticks: { min: 0 } }] }
            }
        }
        var options = object.chartOptionsForDescriptor(descriptor)
        verify(options !== descriptor.options)
        options.legend.labels.display = false
        options.scales.xAxes[0].ticks.min = 7
        compare(descriptor.options.legend.labels.display, true)
        compare(descriptor.options.scales.xAxes[0].ticks.min, 0)
        verify(options.tooltips.callbacks.title !== undefined)
    }

    Component {
        id: anomalyMetricsComponent
        AnomalyEvaluationMetricsPanel {}
    }
    Component {
        id: detectionMetricsComponent
        DetectionEvaluationMetricsPanel {}
    }
    Component {
        id: segmentationImageMetricsComponent
        SegmentationEvaluationImageMetricsPanel {}
    }
    Component {
        id: anomalyChartComponent
        AnomalyEvaluationChartPanel {}
    }
    Component {
        id: detectionChartComponent
        DetectionEvaluationChartPanel {}
    }
    Component {
        id: detectionMatrixComponent
        DetectionConfusionMatrixPanel {}
    }
    Component {
        id: anomalyMatrixComponent
        AnomalyConfusionMatrixPanel {}
    }
    Component {
        id: detectionGridComponent
        DetectionEvaluationInstancesGridView {}
    }
    Component {
        id: anomalyDetailsComponent
        AnomalyEvaluationInstanceDetailsPanel {}
    }
    Component {
        id: paramPanelComponent
        ParamPanel {
            width: 420
            height: 240
        }
    }
    Component {
        id: trainParamsFormComponent
        TrainParamsForm {
            width: 640
            height: 320
        }
    }
    Component {
        id: testTaskPanelComponent
        TestTaskPanel {
            width: 640
            height: 48
        }
    }
    Component {
        id: testPanelComponent
        TestPanel {
            width: 800
            height: 620
        }
    }
}
