import QtQuick
import QtQuick.Layouts

import dltool.data
import dltool.model
import dltool.ui
import quickui

Rectangle {
    id: control

    property IModel selectedModel: null
    property DataManager dataManager: null
    property int partSpacing: 5
    property int scrollbarReserve: 8
    property var trainStatisticsModel: null
    property var validationStatisticsModel: null
    property var emptyChartData: ({
        labels: [],
        datasets: [{ data: [], backgroundColor: [] }]
    })

    radius: 4
    clip: true
    color: QuiColor.Primary

    function ensureStatisticsModels() {
        if (!control.dataManager) {
            control.trainStatisticsModel = null
            control.validationStatisticsModel = null
            return
        }

        if (!control.trainStatisticsModel)
            control.trainStatisticsModel = control.dataManager.createCategoryStatisticsModel()
        if (!control.validationStatisticsModel)
            control.validationStatisticsModel = control.dataManager.createCategoryStatisticsModel()
    }

    function refreshTrainCharts() {
        control.ensureStatisticsModels()
        if (!control.trainStatisticsModel || !trainTree.selectionModel)
            return

        control.trainStatisticsModel.refreshForSelection(trainTree.selectionModel)
    }

    function refreshValidationCharts() {
        control.ensureStatisticsModels()
        if (!control.validationStatisticsModel || !validationTree.selectionModel)
            return

        control.validationStatisticsModel.refreshForSelection(validationTree.selectionModel)
    }

    function refreshAllCharts() {
        control.refreshTrainCharts()
        control.refreshValidationCharts()
    }

    onDataManagerChanged: {
        control.ensureStatisticsModels()
        Qt.callLater(control.refreshAllCharts)
    }

    onSelectedModelChanged: Qt.callLater(control.refreshAllCharts)

    Component.onCompleted: {
        control.ensureStatisticsModels()
        control.refreshAllCharts()
    }

    QuiScrollablePage {
        anchors.fill: parent
        anchors.leftMargin: control.partSpacing
        anchors.topMargin: control.partSpacing
        anchors.rightMargin: 0
        anchors.bottomMargin: control.partSpacing
        animationEnabled: false

        ColumnLayout {
            id: trainDatasetSection
            Layout.fillWidth: true
            Layout.rightMargin: control.scrollbarReserve
            spacing: 6

            DatasetSelectionTreeView {
                id: trainTree
                Layout.fillWidth: true
                roleTitle: qsTr("训练数据集")
                selectionModel: control.selectedModel ? control.selectedModel.trainDatasetViewModel : null
                treeHeight: 180
                onSelectionEdited: control.refreshTrainCharts()
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 205
                spacing: 6

                QuiChart {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    animationDuration: 0
                    chartType: "pie"
                    chartData: control.trainStatisticsModel && trainTree.selectionModel
                               ? control.trainStatisticsModel.imageChartData
                               : control.emptyChartData
                    chartOptions: ({
                        maintainAspectRatio: false,
                        legend: { display: false },
                        title: {
                            display: true,
                            text: qsTr("类别图像占比")
                        },
                        tooltips: {
                            mode: "index",
                            intersect: false
                        }
                    })
                }

                QuiChart {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    animationDuration: 0
                    chartType: "pie"
                    chartData: control.trainStatisticsModel && trainTree.selectionModel
                               ? control.trainStatisticsModel.instanceChartData
                               : control.emptyChartData
                    chartOptions: ({
                        maintainAspectRatio: false,
                        legend: { display: false },
                        title: {
                            display: true,
                            text: qsTr("类别实例占比")
                        },
                        tooltips: {
                            mode: "index",
                            intersect: false
                        }
                    })
                }
            }
        }

        ColumnLayout {
            id: validationDatasetSection
            Layout.fillWidth: true
            Layout.rightMargin: control.scrollbarReserve
            Layout.topMargin: 8
            spacing: 6

            DatasetSelectionTreeView {
                id: validationTree
                Layout.fillWidth: true
                roleTitle: qsTr("验证数据集")
                selectionModel: control.selectedModel ? control.selectedModel.validationDatasetViewModel : null
                treeHeight: 180
                onSelectionEdited: control.refreshValidationCharts()
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 205
                spacing: 6

                QuiChart {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    animationDuration: 0
                    chartType: "pie"
                    chartData: control.validationStatisticsModel && validationTree.selectionModel
                               ? control.validationStatisticsModel.imageChartData
                               : control.emptyChartData
                    chartOptions: ({
                        maintainAspectRatio: false,
                        legend: { display: false },
                        title: {
                            display: true,
                            text: qsTr("类别图像占比")
                        },
                        tooltips: {
                            mode: "index",
                            intersect: false
                        }
                    })
                }

                QuiChart {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    animationDuration: 0
                    chartType: "pie"
                    chartData: control.validationStatisticsModel && validationTree.selectionModel
                               ? control.validationStatisticsModel.instanceChartData
                               : control.emptyChartData
                    chartOptions: ({
                        maintainAspectRatio: false,
                        legend: { display: false },
                        title: {
                            display: true,
                            text: qsTr("类别实例占比")
                        },
                        tooltips: {
                            mode: "index",
                            intersect: false
                        }
                    })
                }
            }
        }

        Connections {
            target: trainTree.selectionModel
            function onSelectionChanged() {
                control.refreshTrainCharts()
            }
        }

        Connections {
            target: validationTree.selectionModel
            function onSelectionChanged() {
                control.refreshValidationCharts()
            }
        }

        Connections {
            target: control.dataManager ? control.dataManager.imageInstances : null
            function onStatsChanged() {
                control.refreshAllCharts()
            }
        }

        Connections {
            target: control.dataManager ? control.dataManager.labelInstances : null
            ignoreUnknownSignals: true
            function onModelReset() {
                control.refreshAllCharts()
            }
            function onRowsInserted() {
                control.refreshAllCharts()
            }
            function onRowsRemoved() {
                control.refreshAllCharts()
            }
        }
    }
}
