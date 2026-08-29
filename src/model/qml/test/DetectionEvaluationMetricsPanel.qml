import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

/*
 * 检测实例指标面板：
 * - 主面板展示全局 micro-F1 与精确率/召回率双饼图。
 * - 点击右上角“按类别详情”弹出独立的类别指标详情窗口（Window）。
 */
EvaluationMetricsPanelBase {
    id: control

    ColumnLayout {
        anchors.fill: parent
        spacing: 2

        Repeater {
            model: control.evaluation && control.evaluation.hasInstanceMetrics
                   ? control.evaluation.instanceMetrics : null
            delegate: ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4

                QuiText {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    font: QuiFont.BodyStrong
                    text: qsTr("F1-Score: %1").arg(control.percentage(model.f1, 2))
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8

                    // 左侧：精确率饼图
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        spacing: 2

                        QuiChart {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 50
                            Layout.minimumHeight: 50
                            animationDuration: 0
                            chartType: EvaluationProtocolKeys.chartKindPie
                            chartData: control.pieData(model.precision)
                            chartOptions: control.pieOptions()
                        }

                        QuiText {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            font: QuiFont.Body
                            text: qsTr("精确率: %1").arg(control.percentage(model.precision, 1))
                        }
                    }

                    // 右侧：召回率饼图
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        spacing: 2

                        QuiChart {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 50
                            Layout.minimumHeight: 50
                            animationDuration: 0
                            chartType: EvaluationProtocolKeys.chartKindPie
                            chartData: control.pieData(model.recall)
                            chartOptions: control.pieOptions()
                        }

                        QuiText {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            font: QuiFont.Body
                            text: qsTr("召回率: %1").arg(control.percentage(model.recall, 1))
                        }
                    }
                }
            }
        }

        QuiText {
            Layout.fillWidth: true
            Layout.fillHeight: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            visible: !control.evaluation || !control.evaluation.hasInstanceMetrics
            text: control.emptyText
            color: QuiColor.FontDark
        }
    }

    // 类别详情独立窗口（Window）
    EvaluationClassDetailsWindow {
        id: classDetailsWindow
        evaluation: control.evaluation
    }

    headerContent: QuiButton {
        anchors.fill: parent
        text: qsTr("按类别详情")
        enabled: !!control.evaluation && control.evaluation.hasInstanceMetrics
        onClicked: {
            classDetailsWindow.show()
            classDetailsWindow.raise()
            classDetailsWindow.requestActivate()
        }
    }
}
