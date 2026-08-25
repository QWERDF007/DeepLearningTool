import QtQuick
import QtQuick.Window
import QtTest

import dltool.core
import dltool.model
import dltool.modeltest 1.0

TestCase {
    id: root
    name: "EvaluationRefactor"

    readonly property string originalImageUrl:
        "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='32' height='32'%3E"
        + "%3Crect width='32' height='32' fill='%23708090'/%3E%3C/svg%3E"
    readonly property string heatmapImageUrl:
        "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='16' height='16'%3E"
        + "%3Crect width='16' height='16' fill='%23e53935'/%3E%3C/svg%3E"

    property var fixture: null
    property var anomalyEvaluation: null
    property var detectionEvaluation: null
    property var segmentationEvaluation: null

    ListModel {
        id: fakeTestTasks

        property bool currentModelBusy: true
        property int currentIndex: 0
        property string currentTaskUuid: "task-1"
        property string currentTaskName: "测试任务"
        property string currentTaskStatus: ""
        property int currentTaskProgress: 0

        ListElement { uuid: "task-1"; name: "测试任务" }
        ListElement { uuid: "task-2"; name: "测试任务 2" }

        function index(row, column) {
            return row
        }

        function data(row, role) {
            return get(Number(row)).uuid
        }

        function switchTask(uuid) {}
        function createTask(name) { return "created" }
        function renameTask(uuid, name) { return true }
        function deleteTask(uuid) { return true }
        function validateTaskName(name) { return "" }
    }

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

    Component {
        id: testTaskPanelComponent
        TestTaskPanel {
            width: 640
            height: 48
            taskManager: fakeTestTasks
        }
    }

    Component {
        id: testPanelComponent
        TestPanel {
            width: 800
            height: 620
        }
    }

    Component {
        id: anomalyThumbnailComponent
        EvaluationInstanceThumbnail {
            width: 180
            height: 140
            record: ({imagePath: "fixture.png",
                      thumbnailUrl: root.originalImageUrl,
                      heatmapUrl: root.heatmapImageUrl,
                      imageWidth: 32,
                      imageHeight: 32,
                      gtBounds: ({x: 8, y: 8, width: 8, height: 8}),
                      predBounds: ({x: 8, y: 8, width: 8, height: 8}),
                      anomalyModelPolygons: [[{x: 2, y: 2}, {x: 6, y: 2}, {x: 6, y: 6}, {x: 2, y: 6}]],
                      anomalyImagePolygons: [[{x: 8, y: 8}, {x: 12, y: 8}, {x: 12, y: 12}, {x: 8, y: 12}]]})
        }
    }

    Component {
        id: anomalyChartComponent
        AnomalyEvaluationChartPanel {
            width: 420
            height: 260
            evaluation: anomalyEvaluation
        }
    }

    Component {
        id: anomalyInstancesGridComponent
        AnomalyEvaluationInstancesGridView {
            width: 800
            height: 300
            evaluation: anomalyEvaluation
        }
    }

    Component {
        id: modelDelegateComponent
        ModelDelegate {
            width: 260
            height: 240
            selected: true
            showTaskActions: true
            taskActionsEnabled: true
            startTaskEnabled: false
            stopTaskEnabled: true
        }
    }

    Component {
        id: modelViewComponent
        ModelView {
            width: 320
            height: 480
            currentModelUuid: "busy-model"
            modelBusyOverride: true
        }
    }

    Component {
        id: trainParamsFormComponent
        TrainParamsForm {
            width: 640
            height: 320
            editable: false
        }
    }

    Component {
        id: trainDatasetsPanelComponent
        TrainDatasetsPanel {
            width: 640
            height: 320
            editable: false
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

    function test_anomalyThumbnailHeatmapLifecycle() {
        testWindow.visible = true
        var thumbnail = createTemporaryObject(anomalyThumbnailComponent, testWindow.contentItem)
        verify(thumbnail)
        var original = findChild(thumbnail, "originalPreview")
        var heatmap = findChild(thumbnail, "heatmapPreview")
        var busy = findChild(thumbnail, "heatmapBusyIndicator")
        var overlay = findChild(thumbnail, "anomalyOverlay")
        verify(original)
        verify(heatmap)
        verify(busy)
        verify(overlay)

        tryCompare(original, "status", Image.Ready, 3000)
        compare(thumbnail.heatmapMode, false)
        compare(original.visible, true)
        compare(heatmap.visible, false)
        compare(overlay.polygons.length, 1)
        compare(overlay.polygons[0][0].x, 8)
        var originalPaintCount = overlay.paintCount

        thumbnail.heatmapEnabled = true
        tryCompare(heatmap, "status", Image.Ready, 3000)
        tryCompare(thumbnail, "heatmapReady", true, 1000)
        tryCompare(thumbnail, "heatmapLoading", false, 1000)
        compare(thumbnail.heatmapMode, true)
        compare(busy.running, false)
        compare(original.visible, false)
        compare(heatmap.visible, true)
        tryCompare(overlay, "visible", true, 1000)
        compare(overlay.width, heatmap.width)
        compare(overlay.height, heatmap.height)
        compare(overlay.polygons.length, 1)
        compare(overlay.polygons[0][0].x, 2)
        tryVerify(function() { return overlay.paintCount > originalPaintCount }, 1000)

        // Repeated mode changes must repaint every time; this catches the
        // intermittent hidden-Canvas race that only appeared in the UI.
        for (var toggleIndex = 0; toggleIndex < 5; ++toggleIndex) {
            var beforeToggle = overlay.paintCount
            thumbnail.heatmapEnabled = false
            tryCompare(thumbnail, "heatmapMode", false, 1000)
            thumbnail.heatmapEnabled = true
            tryCompare(heatmap, "status", Image.Ready, 3000)
            tryCompare(thumbnail, "heatmapMode", true, 1000)
            tryVerify(function() { return overlay.paintCount > beforeToggle }, 1000)
        }

        thumbnail.record = ({imagePath: "fixture.png",
                             thumbnailUrl: root.originalImageUrl,
                             heatmapUrl: "data:image/png;base64,invalid",
                             imageWidth: 32,
                             imageHeight: 32,
                             gtBounds: ({x: 8, y: 8, width: 8, height: 8}),
                             predBounds: ({x: 8, y: 8, width: 8, height: 8}),
                             anomalyModelPolygons: [[{x: 2, y: 2}, {x: 6, y: 2}, {x: 6, y: 6}, {x: 2, y: 6}]],
                             anomalyImagePolygons: [[{x: 8, y: 8}, {x: 12, y: 8}, {x: 12, y: 12}, {x: 8, y: 12}]]})
        tryCompare(heatmap, "status", Image.Error, 3000)
        tryCompare(thumbnail, "heatmapFailed", true, 1000)
        compare(thumbnail.heatmapMode, false)
        compare(busy.running, false)
        compare(original.visible, true)
        compare(heatmap.visible, false)
        compare(overlay.polygons[0][0].x, 8)
        testWindow.visible = false
    }

    function test_anomalyHeatmapToggleLabelFits() {
        testWindow.visible = true
        var grid = createTemporaryObject(anomalyInstancesGridComponent, testWindow.contentItem)
        verify(grid)
        var toggle = findChild(grid, "heatmapToggleSwitch")
        verify(toggle)
        compare(toggle.visible, true)
        compare(toggle.text, "热力图")
        verify(toggle.width >= 100)
        verify(toggle.width >= toggle.contentItem.implicitWidth)
        testWindow.visible = false
    }

    function test_anomalyChartThresholdLineFollowsChartResize() {
        anomalyEvaluation = fixture.createAnomalyEvaluation()
        verify(anomalyEvaluation)
        tryCompare(anomalyEvaluation, "stateKind", ModelEvaluationViewModel.Ready, 5000)

        testWindow.visible = true
        var panel = createTemporaryObject(anomalyChartComponent, testWindow.contentItem)
        verify(panel)
        var line = findChild(panel, "classificationThresholdLine")
        verify(line)
        tryCompare(panel.chart, "chartReady", true, 3000)
        tryVerify(function() { return line.visible && line.height > 0 }, 2000)

        var originalHeight = line.height
        panel.height = 420
        tryVerify(function() { return line.height > originalHeight + 20 }, 2000)
        testWindow.visible = false
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

    function test_anomalyMatrixFilterFirstHeatmapPaintsPolygons() {
        function redStrokePixelCount(image) {
            var count = 0
            for (var y = 0; y < image.height; ++y) {
                for (var x = 0; x < image.width; ++x) {
                    var red = image.red(x, y)
                    var green = image.green(x, y)
                    var blue = image.blue(x, y)
                    if (image.alpha(x, y) > 200 && red > 200
                            && red > green + 80 && red > blue + 80)
                        ++count
                }
            }
            return count
        }

        anomalyEvaluation = fixture.createAnomalyEvaluation()
        verify(anomalyEvaluation)
        tryCompare(anomalyEvaluation, "stateKind", ModelEvaluationViewModel.Ready, 5000)
        anomalyEvaluation.clearMatrixSelection()

        testWindow.visible = true
        var panel = createTemporaryObject(anomalyMatrixComponent, testWindow.contentItem)
        var instances = createTemporaryObject(anomalyInstancesGridComponent, testWindow.contentItem)
        verify(panel)
        verify(instances)
        panel.z = 10
        var matrix = anomalyEvaluation.confusionMatrix
        var anomalyColumns = []
        for (var row = 0; row < matrix.rowCount(); ++row) {
            for (var column = 0; column < matrix.columnCount(); ++column) {
                var index = matrix.index(row, column)
                var rowKey = String(matrix.data(index, EvaluationConfusionModel.RowKeyRole))
                var columnKey = String(matrix.data(index, EvaluationConfusionModel.ColumnKeyRole))
                var count = Number(matrix.data(index, EvaluationConfusionModel.CountRole))
                if (rowKey === "1" && columnKey !== "FP" && columnKey !== "TOTAL" && count > 0)
                    anomalyColumns.push(columnKey)
            }
        }
        compare(anomalyColumns.length, 2)

        var grid = findChild(instances, "evaluationInstancesGrid")
        verify(grid)
        for (var switchIndex = 0; switchIndex < 4; ++switchIndex) {
            instances.heatmapEnabled = false
            var anomalyColumn = anomalyColumns[switchIndex % anomalyColumns.length]
            var cellName = "confusionCell_1_" + anomalyColumn
            panel.visible = true
            tryVerify(function() { return findChild(panel, cellName) !== null }, 2000)

            var anomalyCell = findChild(panel, cellName)
            mouseClick(anomalyCell, anomalyCell.width / 2, anomalyCell.height / 2)
            tryCompare(anomalyEvaluation.filteredInstances, "matrixRow", "1", 1000)
            tryCompare(anomalyEvaluation.filteredInstances, "matrixColumn", anomalyColumn, 1000)
            tryCompare(grid, "count", 1, 2000)
            panel.visible = false
            wait(0)

            var delegate = grid.itemAtIndex(0)
            tryVerify(function() { return grid.itemAtIndex(0) !== null }, 2000)
            delegate = grid.itemAtIndex(0)
            var thumbnail = findChild(delegate, "evaluationInstanceThumbnail")
            verify(thumbnail)
            var overlay = findChild(thumbnail, "anomalyOverlay")
            var heatmap = findChild(thumbnail, "heatmapPreview")
            verify(overlay)
            var overlayCanvas = findChild(overlay, "polygonOverlayCanvas")
            verify(overlayCanvas)
            verify(heatmap)
            tryVerify(function() { return overlay.polygons.length > 0 }, 2000)
            var paintBeforeHeatmap = overlay.paintCount

            // This is the first enable after each matrix filter change. The
            // current delegate's model-coordinate polygons must paint now.
            instances.heatmapEnabled = true
            tryCompare(heatmap, "status", Image.Ready, 3000)
            tryCompare(thumbnail, "heatmapMode", true, 1000)
            tryCompare(overlay, "visible", true, 1000)
            tryVerify(function() { return overlay.polygons.length > 0 }, 1000)
            tryVerify(function() { return overlay.paintCount > paintBeforeHeatmap }, 1000)
            compare(String(overlay.strokeColor).toLowerCase(), "#ff5252")
            wait(16)
            var canvasImage = grabImage(overlayCanvas)
            verify(redStrokePixelCount(canvasImage) > 0,
                   "canvas=" + canvasImage.width + "x" + canvasImage.height
                   + ", viewport=" + JSON.stringify(overlay.coordinateViewport)
                   + ", source=" + overlay.sourceWidth + "x" + overlay.sourceHeight
                   + ", painted=" + overlay.paintedWidth + "x" + overlay.paintedHeight
                   + ", polygons=" + overlay.polygons.length)
        }

        anomalyEvaluation.clearMatrixSelection()
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

    function test_modelBusyLocksTestControlsAndRestoresEditing() {
        var taskPanel = createTemporaryObject(testTaskPanelComponent, testWindow.contentItem)
        verify(taskPanel)
        var taskCombo = findChild(taskPanel, "testTaskCombo")
        var createButton = findChild(taskPanel, "createTestTaskButton")
        var renameButton = findChild(taskPanel, "renameTestTaskButton")
        var deleteButton = findChild(taskPanel, "deleteTestTaskButton")
        verify(taskCombo)
        verify(createButton)
        verify(renameButton)
        verify(deleteButton)
        verify(!taskCombo.enabled)
        verify(!createButton.enabled)
        verify(!renameButton.enabled)
        verify(!deleteButton.enabled)

        fakeTestTasks.currentModelBusy = false
        tryVerify(function() {
            return taskCombo.enabled && createButton.enabled && renameButton.enabled && deleteButton.enabled
        }, 1000)

        var panel = createTemporaryObject(testPanelComponent, testWindow.contentItem)
        verify(panel)
        panel.modelBusy = true
        var datasetPanel = findChild(panel, "testDatasetPanel")
        var inferencePanel = findChild(panel, "testParamsPanel0")
        var evaluationPanel = findChild(panel, "testParamsPanel1")
        verify(datasetPanel)
        verify(inferencePanel)
        verify(evaluationPanel)
        verify(!datasetPanel.enabled)
        verify(!inferencePanel.enabled)
        verify(!evaluationPanel.enabled)

        panel.modelBusy = false
        tryVerify(function() {
            return datasetPanel.enabled && inferencePanel.enabled && evaluationPanel.enabled
        }, 1000)
    }

    function test_runningModelLeavesOnlyStopActionAvailable() {
        var delegate = createTemporaryObject(modelDelegateComponent, testWindow.contentItem)
        verify(delegate)
        var startButton = findChild(delegate, "modelStartTaskButton")
        var stopButton = findChild(delegate, "modelStopTaskButton")
        verify(startButton)
        verify(stopButton)
        verify(!startButton.enabled)
        verify(stopButton.enabled)

        var modelView = createTemporaryObject(modelViewComponent, testWindow.contentItem)
        verify(modelView)
        modelView.currentModelUuid = "busy-model"
        var renameItem = findChild(modelView, "modelRenameMenuItem")
        var deleteItem = findChild(modelView, "modelDeleteMenuItem")
        var copyItem = findChild(modelView, "modelCopyMenuItem")
        verify(renameItem)
        verify(deleteItem)
        verify(copyItem)
        verify(!renameItem.enabled)
        verify(!deleteItem.enabled)
        verify(!copyItem.enabled)
    }

    function test_trainingBusyStateDisablesParameterAndDatasetEditors() {
        var paramsForm = createTemporaryObject(trainParamsFormComponent, testWindow.contentItem)
        var datasetsPanel = createTemporaryObject(trainDatasetsPanelComponent, testWindow.contentItem)
        verify(paramsForm)
        verify(datasetsPanel)
        verify(!paramsForm.enabled)
        verify(!datasetsPanel.enabled)

        paramsForm.editable = true
        datasetsPanel.editable = true
        tryVerify(function() { return paramsForm.enabled && datasetsPanel.enabled }, 1000)
    }
}
