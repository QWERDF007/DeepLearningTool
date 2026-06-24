import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.feature
import dltool.project

import "label"
import "gallery"
import "component"
import quickui

Rectangle {
    id: labelPage
    width: 1080
    height: 1920
    color: QuiColor.Background

    property DataManager dataManager
    readonly property var smartAnnotationController: dataManager ? dataManager.smartAnnotation : null
    readonly property bool smartAnnotationLoading: smartAnnotationController ? smartAnnotationController.loadingModel : false

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
                dataManager: labelPage.dataManager
            }

            LabelImageFlip {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 120
                SplitView.preferredHeight: 120
                color: QuiColor.Primary
                dataManager: labelPage.dataManager
            }

            LabelClassesView {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: QuiColor.Primary
                dataManager: labelPage.dataManager
            }

            ImageTagView {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: QuiColor.Primary
                multiSelect: false
                dataManager: labelPage.dataManager
            }
        }

        Item {
            SplitView.fillHeight: true
            SplitView.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 4

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    color: QuiColor.Primary
                    border.color: QuiColor.Border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6

                        QuiTextIconButton {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            iconSource: QuiFontIcon.Delete
                            text: "删除"
                            enabled: labelCanvas.selection ? labelCanvas.selection.hasSelection : false
                            onClicked: labelCanvas.deleteSelectedLabels()
                        }

                        QuiTextIconButton {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            normalColor: labelCanvas.showBoundingBoxes ? QuiColor.Highlight : QuiColor.Button
                            iconSource: QuiFontIcon.View
                            text: "显示外接矩形"
                            onClicked: labelCanvas.showBoundingBoxes = !labelCanvas.showBoundingBoxes
                        }

                        QuiTextIconButton {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            iconSource: QuiFontIcon.Copy
                            text: "复制"
                            enabled: labelCanvas.selection ? labelCanvas.selection.hasSelection : false
                            onClicked: labelCanvas.copySelectedLabels()
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 4

                    Rectangle {
                        Layout.preferredWidth: 42
                        Layout.fillHeight: true
                        color: QuiColor.Primary
                        border.color: QuiColor.Border

                        ColumnLayout {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.topMargin: 8
                            spacing: 6

                            QuiTextIconButton {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                normalColor: labelCanvas.toolMode === "select" ? QuiColor.Highlight : QuiColor.Button
                                iconSource: QuiFontIcon.TouchPointer
                                text: "选中"
                                onClicked: labelCanvas.setToolMode("select")
                            }

                            QuiTextIconButton {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                normalColor: labelCanvas.toolMode === "rect" ? QuiColor.Highlight : QuiColor.Button
                                iconSource: QuiFontIcon.RectangularClipping
                                text: "绘制矩形"
                                onClicked: labelCanvas.setToolMode("rect")
                            }

                            QuiTextIconButton {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                enabled: labelCanvas.segmentationMode
                                normalColor: labelCanvas.toolMode === "polygon" ? QuiColor.Highlight : QuiColor.Button
                                iconSource: QuiFontIcon.FreeFormClipping
                                text: "绘制多边形"
                                onClicked: labelCanvas.setToolMode("polygon")
                            }

                            QuiTextIconButton {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                enabled: labelCanvas.smartAnnotationAvailable
                                normalColor: labelCanvas.toolMode === "smart" ? QuiColor.Highlight : QuiColor.Button
                                iconSource: QuiFontIcon.Robot
                                text: "智能标注"
                                onClicked: labelCanvas.setToolMode("smart")
                            }

                            QuiTextIconButton {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                enabled: labelPage.dataManager !== null
                                iconSource: QuiFontIcon.TaskView
                                text: "小样本学习"
                                onClicked: fewShotLearningDialog.openForStart()
                            }
                        }
                    }

                    LabelCanvas {
                        id: labelCanvas
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        dataManager: labelPage.dataManager
                    }
                }
            }

            FewShotLearningDialog {
                id: fewShotLearningDialog
                dataManager: labelPage.dataManager
            }

            Connections {
                target: labelPage.smartAnnotationController
                function onModelLoadFinished(success) {
                    if (success && labelCanvas.smartAnnotationMode) {
                        labelCanvas.updateSmartAnnotationPreview()
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                z: 100
                visible: labelPage.smartAnnotationLoading
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
                        running: labelPage.smartAnnotationLoading
                    }

                    QuiText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "正在加载智能标注模型..."
                        color: "white"
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
                id: imageEnhancementPanel
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 200
                zoomValue: labelCanvas.imageScale

                onFitToWindow: {
                    labelCanvas.fitImageInView()
                }

                onZoomChanged: function(zoom) {
                    labelCanvas.setImageScale(zoom)
                }
            }

            LabelsTableView {
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.minimumHeight: 240
                dataManager: labelPage.dataManager
            }

            LabelInstanceEditor {
                id: labelInstanceEditor
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 4 - 20
                visible: hasSelection
                dataManager: labelPage.dataManager
            }

            FileListView {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 4 - 20
                dataManager: labelPage.dataManager
            }
        }
    }
}
