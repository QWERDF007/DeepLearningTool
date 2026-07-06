import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.core
import dltool.ui
import dltool.data
import dltool.feature

import "../gallery"
import "../component"
import quickui

Rectangle {
    id: page
    width: 1080
    height: 1920
    color: QuiColor.Background

    property DataManager dataManager
    property FeatureManager featureManager

    readonly property int method: dataManager ? dataManager.method : -1
    readonly property bool classificationMode: method === DeepLearningMethod.Classification
    readonly property bool segmentationMode: method === DeepLearningMethod.Segmentation
    readonly property bool anomalyMode: method === DeepLearningMethod.AnomalyDetection
    readonly property bool annotationToolbarVisible: !classificationMode
    readonly property bool labelInstanceEditorVisible: !classificationMode
    readonly property SmartAnnotationController smartAnnotation: featureManager ? featureManager.smartAnnotation : null
    readonly property bool smartAnnotationLoading: smartAnnotation ? smartAnnotation.loadingModel : false
    readonly property var activeLabelCanvas: labelCanvasLoader.item

    QuiSplitView {
        anchors.fill: parent
        anchors.margins: 5

        QuiSplitView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 300
            orientation: Qt.Vertical

            DatasetsView {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: QuiColor.Primary
                dataManager: page.dataManager
            }

            Loader {
                id: labelClassesLoader
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                sourceComponent: page.anomalyMode ? anomalyLabelClassesViewComponent : defaultLabelClassesViewComponent
            }

            Component {
                id: defaultLabelClassesViewComponent
                LabelClassesViewBase {
                    anchors.fill: parent
                    dataManager: page.dataManager
                    selectionFollowsCurrentImageClass: page.classificationMode
                }
            }

            Component {
                id: anomalyLabelClassesViewComponent
                AnomalyLabelClassesView {
                    anchors.fill: parent
                    dataManager: page.dataManager
                }
            }

            ImageTagView {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: QuiColor.Primary
                multiSelect: false
                dataManager: page.dataManager
            }
        }

        Item {
            SplitView.fillHeight: true
            SplitView.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 4

                LabelTSidebar {
                    Layout.fillWidth: true
                    Layout.leftMargin: 42
                    Layout.preferredHeight: visible ? 42 : 0
                    visible: page.annotationToolbarVisible
                    hasSelection: page.activeLabelCanvas && page.activeLabelCanvas.selection ? page.activeLabelCanvas.selection.hasSelection : false
                    showBoundingBoxes: page.currentShowBoundingBoxes()
                    onDeleteSelected: page.deleteSelectedLabels()
                    onToggleBoundingBoxes: page.toggleBoundingBoxes()
                    onCopySelected: if (page.activeLabelCanvas && page.activeLabelCanvas.dataManager) page.activeLabelCanvas.dataManager.duplicateSelectedLabels()
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 4

                    LabelLSidebar {
                        Layout.preferredWidth: 42
                        Layout.fillHeight: true
                        currentTool: page.activeLabelCanvas ? page.activeLabelCanvas.toolMode : LabelCanvasEnums.SelectTool
                        featureManager: page.featureManager
                        dataManager: page.dataManager
                        onToolSelected: function(mode) {
                            if (page.activeLabelCanvas) {
                                page.activeLabelCanvas.setToolMode(mode)
                            }
                        }
                    }

                    Loader {
                        id: labelCanvasLoader
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        sourceComponent: page.labelCanvasComponent()
                    }

                    Component {
                        id: detectionLabelCanvasComponent
                        DetectionLabelCanvas {
                            anchors.fill: parent
                            dataManager: page.dataManager
                            featureManager: page.featureManager
                        }
                    }

                    Component {
                        id: classificationLabelCanvasComponent
                        ClassificationLabelCanvas {
                            anchors.fill: parent
                            dataManager: page.dataManager
                        }
                    }

                    Component {
                        id: segmentationLabelCanvasComponent
                        SegmentationLabelCanvas {
                            anchors.fill: parent
                            dataManager: page.dataManager
                            featureManager: page.featureManager
                        }
                    }

                    Component {
                        id: anomalyLabelCanvasComponent
                        AnomalyLabelCanvas {
                            anchors.fill: parent
                            dataManager: page.dataManager
                            featureManager: page.featureManager
                        }
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                z: 100
                visible: page.smartAnnotationLoading
                color: "#66000000"

                MouseArea {
                    anchors.fill: parent
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 10

                    BusyIndicator {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 48
                        height: 48
                        running: page.smartAnnotationLoading
                    }

                    QuiText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "正在加载智能标注模型..."
                        font: QuiFont.Body
                    }
                }
            }
        }

        QuiSplitView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 320
            orientation: Qt.Vertical

            ImageEnhancementPanel {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 240
                SplitView.preferredHeight: 240
                zoomValue: page.activeLabelCanvas ? page.activeLabelCanvas.imageView.image.scale : 1

                onFitToWindow: {
                    if (page.activeLabelCanvas) {
                        page.activeLabelCanvas.imageView.fitInView()
                    }
                }

                onZoomChanged: function(zoom) {
                    if (page.activeLabelCanvas) {
                        page.activeLabelCanvas.imageView.scaleInCenter(zoom)
                    }
                }
            }

            LabelsTableView {
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 240
                dataManager: page.dataManager
            }

            LabelInstanceEditor {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: visible ? parent.height / 4 - 20 : 0
                visible: page.labelInstanceEditorVisible && hasSelection
                dataManager: page.dataManager
            }

            FileListView {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 4 - 20
                dataManager: page.dataManager
            }
        }
    }

    function currentShowBoundingBoxes() {
        if (!annotationToolbarVisible || !activeLabelCanvas) {
            return false
        }

        let value = activeLabelCanvas.showBoundingBoxes
        return value === undefined ? false : value
    }

    function deleteSelectedLabels() {
        if (!annotationToolbarVisible || !activeLabelCanvas || !activeLabelCanvas.actions) {
            return
        }

        activeLabelCanvas.actions.deleteSelectedLabels()
    }

    function toggleBoundingBoxes() {
        if (!annotationToolbarVisible || !activeLabelCanvas) {
            return
        }

        let value = activeLabelCanvas.showBoundingBoxes
        if (value === undefined) {
            return
        }
        activeLabelCanvas.showBoundingBoxes = !value
    }

    function labelCanvasComponent() {
        if (classificationMode) {
            return classificationLabelCanvasComponent
        }
        if (anomalyMode) {
            return anomalyLabelCanvasComponent
        }
        if (segmentationMode) {
            return segmentationLabelCanvasComponent
        }
        return detectionLabelCanvasComponent
    }
}
