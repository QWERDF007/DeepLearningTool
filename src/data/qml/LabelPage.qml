import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.project

import "label"
import "gallery"
import "component"

Rectangle {
    id: labelPage
    width: 1080
    height: 1920
    color: DltColor.Background

    property DataManager dataManager
    readonly property var smartAnnotationController: dataManager ? dataManager.smartAnnotation : null
    readonly property bool smartAnnotationLoading: smartAnnotationController ? smartAnnotationController.loadingModel : false

    DltSplitView {
        anchors.fill: parent
        anchors.margins: 5

        DltSplitView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 300
            orientation: Qt.Vertical

            DatasetsView {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: DltColor.Primary
                dataManager: labelPage.dataManager
            }

            LabelImageFlip {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 120
                SplitView.preferredHeight: 120
                color: DltColor.Primary
                dataManager: labelPage.dataManager
            }

            LabelClassesView {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: DltColor.Primary
                dataManager: labelPage.dataManager
            }

            ImageTagView {
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: DltColor.Primary
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
                    color: DltColor.Primary
                    border.color: DltColor.Border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6

                        DltTextIconButton {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            iconSource: DltFontIcon.Delete
                            text: "删除"
                            enabled: labelCanvas.selection ? labelCanvas.selection.hasSelection : false
                            onClicked: labelCanvas.deleteSelectedLabels()
                        }

                        DltTextIconButton {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            normalColor: labelCanvas.showBoundingBoxes ? DltColor.Highlight : DltColor.Button
                            iconSource: DltFontIcon.View
                            text: "显示外接矩形"
                            onClicked: labelCanvas.showBoundingBoxes = !labelCanvas.showBoundingBoxes
                        }

                        DltTextIconButton {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            iconSource: DltFontIcon.Copy
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
                        color: DltColor.Primary
                        border.color: DltColor.Border

                        ColumnLayout {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.topMargin: 8
                            spacing: 6

                            DltTextIconButton {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                normalColor: labelCanvas.toolMode === "select" ? DltColor.Highlight : DltColor.Button
                                iconSource: DltFontIcon.TouchPointer
                                text: "选中"
                                onClicked: labelCanvas.setToolMode("select")
                            }

                            DltTextIconButton {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                normalColor: labelCanvas.toolMode === "rect" ? DltColor.Highlight : DltColor.Button
                                iconSource: DltFontIcon.RectangularClipping
                                text: "绘制矩形"
                                onClicked: labelCanvas.setToolMode("rect")
                            }

                            DltTextIconButton {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                enabled: labelCanvas.segmentationMode
                                normalColor: labelCanvas.toolMode === "polygon" ? DltColor.Highlight : DltColor.Button
                                iconSource: DltFontIcon.FreeFormClipping
                                text: "绘制多边形"
                                onClicked: labelCanvas.setToolMode("polygon")
                            }

                            DltTextIconButton {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                enabled: labelCanvas.smartAnnotationAvailable
                                normalColor: labelCanvas.toolMode === "smart" ? DltColor.Highlight : DltColor.Button
                                iconSource: DltFontIcon.Robot
                                text: "智能标注"
                                onClicked: labelCanvas.setToolMode("smart")
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

                    DltText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "正在加载智能标注模型..."
                        color: "white"
                        font: DltFont.Body
                    }
                }
            }
        }

        DltSplitView {
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
