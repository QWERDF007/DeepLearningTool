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
    readonly property bool annotationToolbarVisible: !classificationMode
    readonly property bool labelInstanceEditorVisible: !classificationMode
    readonly property SmartAnnotationController smartAnnotation: featureManager ? featureManager.smartAnnotation : null
    readonly property bool smartAnnotationLoading: smartAnnotation ? smartAnnotation.loadingModel : false

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

            LabelClassesView {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                dataManager: page.dataManager
                selectionFollowsCurrentImageClass: page.classificationMode
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
                    hasSelection: labelCanvas.selection ? labelCanvas.selection.hasSelection : false
                    showBoundingBoxes: labelCanvas.showBoundingBoxes
                    onDeleteSelected: labelCanvas.actions.deleteSelectedLabels()
                    onToggleBoundingBoxes: labelCanvas.showBoundingBoxes = !labelCanvas.showBoundingBoxes
                    onCopySelected: if (labelCanvas.dataManager) labelCanvas.dataManager.duplicateSelectedLabels()
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 4

                    LabelLSidebar {
                        Layout.preferredWidth: 42
                        Layout.fillHeight: true
                        currentTool: labelCanvas.toolMode
                        featureManager: page.featureManager
                        dataManager: page.dataManager
                        onToolSelected: function(mode) {
                            labelCanvas.setToolMode(mode)
                        }
                    }

                    LabelCanvas {
                        id: labelCanvas
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        dataManager: page.dataManager
                        featureManager: page.featureManager
                        classificationMode: page.classificationMode
                        segmentationMode: page.segmentationMode
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
                zoomValue: labelCanvas.imageView.image.scale

                onFitToWindow: {
                    labelCanvas.imageView.fitInView()
                }

                onZoomChanged: function(zoom) {
                    labelCanvas.imageView.scaleInCenter(zoom)
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
}
