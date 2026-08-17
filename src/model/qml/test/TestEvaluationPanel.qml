import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

Item {
    id: control
    property ModelEvaluationViewModel evaluation: null
    property ITestParams testParams: null

    EvaluationPanelRegistry {
        id: panelRegistry
    }

    function bindPanel(loader) {
        if (!loader || !loader.item)
            return
        loader.item.evaluation = Qt.binding(function() { return control.evaluation })
        if ("testParams" in loader.item)
            loader.item.testParams = Qt.binding(function() { return control.testParams })
    }

    QuiSplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        QuiSplitView {
            id: summarySplit
            SplitView.fillWidth: true
            SplitView.preferredHeight: 240
            orientation: Qt.Horizontal

            Loader {
                SplitView.fillHeight: true
                SplitView.preferredWidth: 260
                SplitView.minimumWidth: 180
                sourceComponent: control.evaluation
                                 ? panelRegistry.metricsPanel(control.evaluation.method) : null
                onLoaded: control.bindPanel(this)
            }

            Loader {
                SplitView.fillHeight: true
                SplitView.preferredWidth: 260
                SplitView.minimumWidth: 180
                sourceComponent: control.evaluation
                                 ? panelRegistry.imageMetricsPanel(control.evaluation.method) : null
                onLoaded: control.bindPanel(this)
            }

            Loader {
                id: chartPanelLoader
                SplitView.fillHeight: true
                SplitView.fillWidth: true
                SplitView.preferredWidth: 420
                SplitView.minimumWidth: 260
                sourceComponent: control.evaluation
                                 ? panelRegistry.chartPanel(control.evaluation.method) : null
                onLoaded: control.bindPanel(this)
            }
        }

        QuiSplitView {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            orientation: Qt.Horizontal

            Loader {
                SplitView.fillHeight: true
                SplitView.preferredWidth: 480
                SplitView.minimumWidth: 320
                sourceComponent: control.evaluation
                                 ? panelRegistry.confusionMatrixPanel(control.evaluation.method) : null
                onLoaded: control.bindPanel(this)
            }

            Loader {
                SplitView.fillHeight: true
                SplitView.fillWidth: true
                SplitView.preferredWidth: 480
                SplitView.minimumWidth: 280
                sourceComponent: control.evaluation
                                 ? panelRegistry.instancesGridPanel(control.evaluation.method) : null
                onLoaded: control.bindPanel(this)
            }

            Loader {
                SplitView.fillHeight: true
                SplitView.preferredWidth: 320
                SplitView.minimumWidth: 240
                sourceComponent: control.evaluation
                                 ? panelRegistry.instanceDetailsPanel(control.evaluation.method) : null
                onLoaded: control.bindPanel(this)
            }
        }
    }

    Rectangle {
        id: stateOverlay
        anchors.fill: parent
        z: 20
        visible: !control.evaluation || control.evaluation.loading
                 || control.evaluation.stateKind !== ModelEvaluationViewModel.Ready
        color: QuiColor.Background

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - 32, 520)
            spacing: 10
            QuiProgressBar {
                width: 180
                height: 14
                anchors.horizontalCenter: parent.horizontalCenter
                indeterminate: !!control.evaluation && control.evaluation.loading
                textVisible: false
                visible: indeterminate
            }
            QuiText {
                anchors.horizontalCenter: parent.horizontalCenter
                text: {
                    if (!control.evaluation)
                        return qsTr("请选择测试任务")
                    switch (control.evaluation.stateKind) {
                    case ModelEvaluationViewModel.Loading: return qsTr("正在后台评估…")
                    case ModelEvaluationViewModel.Running: return qsTr("测试任务运行中，等待评估")
                    case ModelEvaluationViewModel.Failed: return qsTr("测试任务失败，暂无可用评估结果")
                    case ModelEvaluationViewModel.MissingResult:
                        return qsTr("当前测试任务还没有可评估的预测结果")
                    case ModelEvaluationViewModel.InvalidResult: return qsTr("评估数据无效，无法展示")
                    case ModelEvaluationViewModel.Error: return qsTr("后台评估失败")
                    default: return qsTr("暂无评估结果")
                    }
                }
            }
            QuiText {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: !!control.evaluation && control.evaluation.error.length > 0
                text: control.evaluation ? control.evaluation.error : ""
                color: QuiColor.FontDark
            }
        }
    }
}
