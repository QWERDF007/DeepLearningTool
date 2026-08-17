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
            { tag: "AnomalyDetails", component: anomalyDetailsComponent }
        ]
    }

    function test_componentsLoad(data) {
        var object = createTemporaryObject(data.component, null)
        verify(object, data.tag)
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
}
