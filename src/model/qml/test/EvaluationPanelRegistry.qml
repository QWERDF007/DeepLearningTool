import QtQml

import dltool.core
import dltool.model
import dltool.ui
import quickui

/*
 * 评估面板注册表（QML 单例）：
 *
 * - 集中维护 method -> 子面板组件映射，TestEvaluationPanel 只负责布局。
 * - 返回的是 Component；Loader 加载后由页面设置 evaluation/testParams。
 * - 新增方法时只需在此追加组件，不改页面 switch。
 */
QtObject {
    id: root

    readonly property Component anomalyMetricsPanelComponent: Component {
        AnomalyEvaluationMetricsPanel {}
    }
    readonly property Component detectionMetricsPanelComponent: Component {
        DetectionEvaluationMetricsPanel {}
    }
    readonly property Component segmentationMetricsPanelComponent: Component {
        SegmentationEvaluationMetricsPanel {}
    }

    readonly property Component anomalyImageMetricsPanelComponent: Component {
        AnomalyEvaluationImageMetricsPanel {}
    }
    readonly property Component detectionImageMetricsPanelComponent: Component {
        DetectionEvaluationImageMetricsPanel {}
    }
    readonly property Component segmentationImageMetricsPanelComponent: Component {
        SegmentationEvaluationImageMetricsPanel {}
    }

    readonly property Component anomalyChartPanelComponent: Component {
        AnomalyEvaluationChartPanel {}
    }
    readonly property Component detectionChartPanelComponent: Component {
        DetectionEvaluationChartPanel {}
    }
    readonly property Component segmentationChartPanelComponent: Component {
        DetectionEvaluationChartPanel {}
    }

    readonly property Component anomalyConfusionMatrixPanelComponent: Component {
        AnomalyConfusionMatrixPanel {}
    }
    readonly property Component detectionConfusionMatrixPanelComponent: Component {
        DetectionConfusionMatrixPanel {}
    }
    readonly property Component segmentationConfusionMatrixPanelComponent: Component {
        SegmentationConfusionMatrixPanel {}
    }

    readonly property Component anomalyInstancesGridPanelComponent: Component {
        AnomalyEvaluationInstancesGridView {}
    }
    readonly property Component detectionInstancesGridPanelComponent: Component {
        DetectionEvaluationInstancesGridView {}
    }
    readonly property Component segmentationInstancesGridPanelComponent: Component {
        SegmentationEvaluationInstancesGridView {}
    }

    readonly property Component anomalyInstanceDetailsPanelComponent: Component {
        AnomalyEvaluationInstanceDetailsPanel {}
    }
    readonly property Component detectionInstanceDetailsPanelComponent: Component {
        DetectionEvaluationInstanceDetailsPanel {}
    }
    readonly property Component segmentationInstanceDetailsPanelComponent: Component {
        SegmentationEvaluationInstanceDetailsPanel {}
    }

    function isAnomalyMethod(method) {
        return Number(method) === DeepLearningMethod.AnomalyDetection
    }

    function isSegmentationMethod(method) {
        return Number(method) === DeepLearningMethod.Segmentation
    }

    function metricsPanel(method) {
        if (root.isAnomalyMethod(method))
            return root.anomalyMetricsPanelComponent
        if (root.isSegmentationMethod(method))
            return root.segmentationMetricsPanelComponent
        return root.detectionMetricsPanelComponent
    }

    function imageMetricsPanel(method) {
        if (root.isAnomalyMethod(method))
            return root.anomalyImageMetricsPanelComponent
        if (root.isSegmentationMethod(method))
            return root.segmentationImageMetricsPanelComponent
        return root.detectionImageMetricsPanelComponent
    }

    function chartPanel(method) {
        if (root.isAnomalyMethod(method))
            return root.anomalyChartPanelComponent
        if (root.isSegmentationMethod(method))
            return root.segmentationChartPanelComponent
        return root.detectionChartPanelComponent
    }

    function confusionMatrixPanel(method) {
        if (root.isAnomalyMethod(method))
            return root.anomalyConfusionMatrixPanelComponent
        if (root.isSegmentationMethod(method))
            return root.segmentationConfusionMatrixPanelComponent
        return root.detectionConfusionMatrixPanelComponent
    }

    function instancesGridPanel(method) {
        if (root.isAnomalyMethod(method))
            return root.anomalyInstancesGridPanelComponent
        if (root.isSegmentationMethod(method))
            return root.segmentationInstancesGridPanelComponent
        return root.detectionInstancesGridPanelComponent
    }

    function instanceDetailsPanel(method) {
        if (root.isAnomalyMethod(method))
            return root.anomalyInstanceDetailsPanelComponent
        if (root.isSegmentationMethod(method))
            return root.segmentationInstanceDetailsPanelComponent
        return root.detectionInstanceDetailsPanelComponent
    }
}
