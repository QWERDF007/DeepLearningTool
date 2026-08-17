import QtQuick
import QtTest

import dltool.core
import dltool.model

TestCase {
    name: "EvaluationPanelRegistry"

    property var registry: null

    function init() {
        registry = createTemporaryObject(evaluationPanelRegistryComponent, null)
        verify(registry)
    }

    Component {
        id: evaluationPanelRegistryComponent
        EvaluationPanelRegistry {}
    }

    function test_detectionMapping_data() {
        return [
            { method: DeepLearningMethod.Detection, panel: "metrics" },
            { method: DeepLearningMethod.Detection, panel: "imageMetrics" },
            { method: DeepLearningMethod.Detection, panel: "chart" },
            { method: DeepLearningMethod.Detection, panel: "confusionMatrix" },
            { method: DeepLearningMethod.Detection, panel: "instancesGrid" },
            { method: DeepLearningMethod.Detection, panel: "instanceDetails" }
        ]
    }

    function test_detectionMapping(data) {
        var component = resolvePanel(registry, data.method, data.panel)
        verify(component)
    }

    function test_anomalyMapping_data() {
        return [
            { method: DeepLearningMethod.AnomalyDetection, panel: "metrics" },
            { method: DeepLearningMethod.AnomalyDetection, panel: "imageMetrics" },
            { method: DeepLearningMethod.AnomalyDetection, panel: "chart" },
            { method: DeepLearningMethod.AnomalyDetection, panel: "confusionMatrix" },
            { method: DeepLearningMethod.AnomalyDetection, panel: "instancesGrid" },
            { method: DeepLearningMethod.AnomalyDetection, panel: "instanceDetails" }
        ]
    }

    function test_anomalyMapping(data) {
        var component = resolvePanel(registry, data.method, data.panel)
        verify(component)
    }



    function test_segmentationMapping() {
        verify(registry.metricsPanel(DeepLearningMethod.Segmentation))
        verify(registry.imageMetricsPanel(DeepLearningMethod.Segmentation))
        verify(registry.chartPanel(DeepLearningMethod.Segmentation))
        verify(registry.confusionMatrixPanel(DeepLearningMethod.Segmentation))
        verify(registry.instancesGridPanel(DeepLearningMethod.Segmentation))
        verify(registry.instanceDetailsPanel(DeepLearningMethod.Segmentation))
    }

    function resolvePanel(reg, method, panel) {
        if (!reg)
            return null
        switch (panel) {
        case "metrics":
            return reg.metricsPanel(method)
        case "imageMetrics":
            return reg.imageMetricsPanel(method)
        case "chart":
            return reg.chartPanel(method)
        case "confusionMatrix":
            return reg.confusionMatrixPanel(method)
        case "instancesGrid":
            return reg.instancesGridPanel(method)
        case "instanceDetails":
            return reg.instanceDetailsPanel(method)
        }
        return null
    }
}
