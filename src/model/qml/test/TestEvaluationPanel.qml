import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

Rectangle {
    id: control
    color: QuiColor.Background
    property ModelEvaluationViewModel evaluation: null

    onHeightChanged: {
        console.log("height", height)
    }

    QuiSplitView {
        anchors.fill: parent
        anchors.margins: 6
        orientation: Qt.Vertical

        QuiSplitView {
            id: summarySplit
            SplitView.fillWidth: true
            SplitView.preferredHeight: 240
            orientation: Qt.Horizontal
            
            EvaluationMetricsPanel {
                SplitView.fillHeight: true
                SplitView.preferredWidth: 220 
                evaluation: control.evaluation 
            }
            
            EvaluationImageMetricsPanel { 
                SplitView.fillHeight: true
                SplitView.preferredWidth: 220  
                evaluation: control.evaluation 
            }
            
            EvaluationChartPanel { 
                SplitView.fillHeight: true
                SplitView.fillWidth: true
                SplitView.preferredWidth: 420
                SplitView.minimumWidth: 260
                evaluation: control.evaluation 
            }
        }

        QuiSplitView {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            orientation: Qt.Horizontal

            QuiFrame {
                SplitView.fillHeight: true
                SplitView.preferredWidth: 480
                ConfusionMatrixPanel { anchors.fill: parent; evaluation: control.evaluation }
            }
            QuiFrame {
                SplitView.fillHeight: true
                SplitView.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    QuiText { 
                        text: qsTr("实例图像")
                        font: QuiFont.Subtitle
                    }
                    EvaluationInstancesGridView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        evaluation: control.evaluation
                    }
                }
            }
            QuiFrame {
                SplitView.fillHeight: true
                SplitView.preferredWidth: 320
                EvaluationInstanceDetailsPanel { anchors.fill: parent; evaluation: control.evaluation }
            }
        }
    }

    Rectangle {
        id: stateOverlay
        anchors.fill: parent
        z: 20
        visible: !control.evaluation || control.evaluation.loading || control.evaluation.state !== "Ready"
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
                    switch (control.evaluation.state) {
                    case "Loading": return qsTr("正在后台评估…")
                    case "Running": return qsTr("测试任务运行中，等待评估")
                    case "Failed": return qsTr("测试任务失败，暂无可用评估结果")
                    case "MissingResult": return qsTr("当前测试任务还没有可评估的预测结果")
                    case "InvalidResult": return qsTr("评估数据无效，无法展示")
                    case "Error": return qsTr("后台评估失败")
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
