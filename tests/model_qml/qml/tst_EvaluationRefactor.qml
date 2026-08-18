import QtQuick
import QtQuick.Window
import QtTest

import dltool.core
import dltool.model
import dltool.modeltest 1.0

TestCase {
    name: "EvaluationRefactor"

    property var fixture: null
    property var anomalyEvaluation: null
    property var detectionEvaluation: null
    property var segmentationEvaluation: null

    ModelTestFixture { id: fixtureObject }

    Window {
        id: testWindow
        width: 800
        height: 620
        visible: false
    }

    Component {
        id: anomalyMatrixComponent
        AnomalyConfusionMatrixPanel {
            width: 720
            height: 520
            evaluation: anomalyEvaluation
        }
    }

    Component {
        id: detectionMatrixComponent
        DetectionConfusionMatrixPanel {
            width: 720
            height: 520
            evaluation: detectionEvaluation
        }
    }

    Component {
        id: evaluationPanelComponent
        TestEvaluationPanel {
            width: 800
            height: 620
        }
    }

    Component {
        id: panelRegistryComponent
        EvaluationPanelRegistry {}
    }

    Component {
        id: injectedMetricsComponent
        EvaluationMetricsPanelBase {
            width: 320
            height: 220
            Item {
                objectName: "injectedMetricsContent"
            }
        }
    }

    Component {
        id: detectionChartComponent
        DetectionEvaluationChartPanel {
            width: 420
            height: 260
        }
    }

    function init() {
        fixture = fixtureObject
    }

    function test_protocolKeysAreTheQmlBoundary() {
        compare(EvaluationProtocolKeys.chartKindLine, "line")
        compare(EvaluationProtocolKeys.chartKindPie, "pie")
        compare(EvaluationProtocolKeys.chartIdPrecisionRecall, "precision_recall")
        compare(EvaluationProtocolKeys.matrixAxisFalseNegative, "FN")
        compare(EvaluationProtocolKeys.matrixAxisFalsePositive, "FP")
        compare(EvaluationProtocolKeys.matrixAxisTotal, "TOTAL")
        compare(EvaluationProtocolKeys.matrixAxisKey(0), "FN")
        compare(EvaluationProtocolKeys.fieldName(0), "schema_version")
    }

    function test_basePropertyAcceptsConcreteViewModelRuntimeExtensions() {
        var anomaly = fixture.createAnomalyEvaluation()
        compare(anomaly !== null, true)
        compare(anomaly.classificationThreshold, 0.5)

        var detection = fixture.createDetectionViewModel()
        verify(detection.precisionRecallClasses !== undefined)
    }

    function test_anomalyMatrixClickSetsFnAndFpFilters() {
        anomalyEvaluation = fixture.createAnomalyEvaluation()
        verify(anomalyEvaluation)
        tryCompare(anomalyEvaluation, "stateKind", ModelEvaluationViewModel.Ready, 5000)
        tryVerify(function() { return anomalyEvaluation.confusionMatrix.rowCount() === 4 }, 2000)

        testWindow.visible = true
        var panel = createTemporaryObject(anomalyMatrixComponent, testWindow.contentItem)
        verify(panel)
        tryVerify(function() { return panel.width > 0 && panel.height > 0 }, 1000)
        tryVerify(function() { return findChild(panel, "confusionCell_FN_1") !== null }, 2000)

        var fnCell = findChild(panel, "confusionCell_FN_1")
        verify(fnCell)
        mouseClick(fnCell, fnCell.width / 2, fnCell.height / 2)
        tryCompare(anomalyEvaluation.filteredInstances, "matrixRow", "FN", 1000)
        tryCompare(anomalyEvaluation.filteredInstances, "matrixColumn", "1", 1000)

        anomalyEvaluation.clearMatrixSelection()
        var fpCell = findChild(panel, "confusionCell_1_FP")
        verify(fpCell)
        mouseClick(fpCell, fpCell.width / 2, fpCell.height / 2)
        tryCompare(anomalyEvaluation.filteredInstances, "matrixRow", "1", 1000)
        tryCompare(anomalyEvaluation.filteredInstances, "matrixColumn", "FP", 1000)
        testWindow.visible = false
    }

    function test_everyRegisteredPanelCanInstantiate() {
        var registry = createTemporaryObject(panelRegistryComponent, null)
        verify(registry)
        var methods = [DeepLearningMethod.Detection,
                       DeepLearningMethod.Segmentation,
                       DeepLearningMethod.AnomalyDetection]
        var panelFunctions = ["metricsPanel", "imageMetricsPanel", "chartPanel",
                              "confusionMatrixPanel", "instancesGridPanel", "instanceDetailsPanel"]
        for (var methodIndex = 0; methodIndex < methods.length; ++methodIndex) {
            for (var panelIndex = 0; panelIndex < panelFunctions.length; ++panelIndex) {
                var component = registry[panelFunctions[panelIndex]](methods[methodIndex])
                verify(component, panelFunctions[panelIndex] + " method " + methods[methodIndex])
                var object = createTemporaryObject(component, null)
                verify(object, "failed to instantiate " + panelFunctions[panelIndex])
            }
        }
    }

    function test_baseContentInjectionAndMethodChartHook() {
        var metrics = createTemporaryObject(injectedMetricsComponent, null)
        verify(metrics)
        verify(findChild(metrics, "injectedMetricsContent") !== null)
        compare(metrics.percentage(0.25, 2), "25.00%")

        var chart = createTemporaryObject(detectionChartComponent, null)
        verify(chart)
        var input = {
            labels: [0, 1],
            datasets: [
                { series_kind: "class", class_id: 1, data: [1] },
                { series_kind: "class", class_id: 2, data: [2] },
                { series_kind: "average", data: [3] }
            ]
        }
        var transformed = chart.transformChartData("precision_recall", input)
        compare(transformed.datasets.length, 2)
        compare(Number(transformed.datasets[1].class_id), 1)
        chart.selectedPrecisionRecallClassId = 2
        var selectedInput = {
            labels: [0, 1],
            datasets: [
                { series_kind: "class", class_id: 1, data: [1] },
                { series_kind: "class", class_id: 2, data: [2] },
                { series_kind: "average", data: [3] }
            ]
        }
        transformed = chart.transformChartData("precision_recall", selectedInput)
        compare(transformed.datasets.length, 2)
        compare(Number(transformed.datasets[0].class_id), 2)
    }

    function test_testEvaluationPanelReadyStatesAndRuntimeStateOverlay() {
        testWindow.visible = true
        var panel = createTemporaryObject(evaluationPanelComponent, testWindow.contentItem)
        verify(panel)
        var overlay = findChild(panel, "evaluationStateOverlay")
        verify(overlay)
        compare(overlay.visible, true)

        detectionEvaluation = fixture.createDetectionViewModel()
        verify(detectionEvaluation)
        tryCompare(detectionEvaluation, "stateKind", ModelEvaluationViewModel.Ready, 5000)
        panel.evaluation = detectionEvaluation
        tryVerify(function() { return overlay.visible === false }, 3000)
        compare(detectionEvaluation.hasInstanceMetrics, true)
        compare(detectionEvaluation.hasImageMetrics, true)

        segmentationEvaluation = fixture.createSegmentationEvaluation()
        verify(segmentationEvaluation)
        tryCompare(segmentationEvaluation, "stateKind", ModelEvaluationViewModel.Ready, 5000)
        panel.evaluation = segmentationEvaluation
        tryVerify(function() { return overlay.visible === false }, 3000)
        compare(segmentationEvaluation.method, DeepLearningMethod.Segmentation)

        segmentationEvaluation.setRuntimeState(ModelEvaluationViewModel.Running)
        tryCompare(segmentationEvaluation, "stateKind", ModelEvaluationViewModel.Running, 1000)
        tryVerify(function() { return overlay.visible === true }, 1000)
        segmentationEvaluation.setRuntimeState(ModelEvaluationViewModel.Failed)
        tryCompare(segmentationEvaluation, "stateKind", ModelEvaluationViewModel.Failed, 1000)
        segmentationEvaluation.invalidate(ModelEvaluationViewModel.MissingResult)
        tryCompare(segmentationEvaluation, "stateKind", ModelEvaluationViewModel.MissingResult, 1000)
        segmentationEvaluation.invalidate(ModelEvaluationViewModel.InvalidResult)
        tryCompare(segmentationEvaluation, "stateKind", ModelEvaluationViewModel.InvalidResult, 1000)
        segmentationEvaluation.invalidate(ModelEvaluationViewModel.Error)
        tryCompare(segmentationEvaluation, "stateKind", ModelEvaluationViewModel.Error, 1000)
        testWindow.visible = false
    }

    function test_detectionMatrixDelegatePropertiesAndClickFilters() {
        detectionEvaluation = fixture.createDetectionViewModel()
        verify(detectionEvaluation)
        tryCompare(detectionEvaluation, "stateKind", ModelEvaluationViewModel.Ready, 5000)
        testWindow.visible = true
        var panel = createTemporaryObject(detectionMatrixComponent, testWindow.contentItem)
        verify(panel)

        var matrix = detectionEvaluation.confusionMatrix
        var classKey = ""
        for (var row = 0; row < matrix.rowCount() && classKey === ""; ++row) {
            var rowKey = String(matrix.data(matrix.index(row, 0), EvaluationConfusionModel.RowKeyRole))
            if (rowKey !== EvaluationProtocolKeys.matrixAxisFalseNegative
                    && rowKey !== EvaluationProtocolKeys.matrixAxisFalsePositive
                    && rowKey !== EvaluationProtocolKeys.matrixAxisTotal)
                classKey = rowKey
        }
        verify(classKey.length > 0)
        tryVerify(function() { return findChild(panel, "confusionCell_" + classKey + "_" + classKey) !== null }, 3000)
        var normalCell = findChild(panel, "confusionCell_" + classKey + "_" + classKey)
        verify(normalCell)
        compare(normalCell.rowKey, classKey)
        compare(normalCell.columnKey, classKey)
        compare(normalCell.cellKind, EvaluationConfusionModel.CellKindMatch)
        verify(normalCell.selectable)

        var fnCell = findChild(panel, "confusionCell_" + EvaluationProtocolKeys.matrixAxisFalseNegative + "_" + classKey)
        var fpCell = findChild(panel, "confusionCell_" + classKey + "_" + EvaluationProtocolKeys.matrixAxisFalsePositive)
        verify(fnCell)
        verify(fpCell)
        compare(fnCell.cellKind, EvaluationConfusionModel.CellKindFalseNegative)
        compare(fpCell.cellKind, EvaluationConfusionModel.CellKindFalsePositive)
        mouseClick(fnCell, fnCell.width / 2, fnCell.height / 2)
        tryCompare(detectionEvaluation.filteredInstances, "matrixRow", EvaluationProtocolKeys.matrixAxisFalseNegative, 1000)
        tryCompare(detectionEvaluation.filteredInstances, "matrixColumn", classKey, 1000)
        detectionEvaluation.clearMatrixSelection()
        mouseClick(fpCell, fpCell.width / 2, fpCell.height / 2)
        tryCompare(detectionEvaluation.filteredInstances, "matrixRow", classKey, 1000)
        tryCompare(detectionEvaluation.filteredInstances, "matrixColumn", EvaluationProtocolKeys.matrixAxisFalsePositive, 1000)
        testWindow.visible = false
    }
}
