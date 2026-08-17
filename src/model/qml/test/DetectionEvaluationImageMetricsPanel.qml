import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

EvaluationImageMetricsPanelBase {
    id: control

    ColumnLayout {
        anchors.fill: parent
        spacing: 2

        Repeater {
            model: control.evaluation && control.evaluation.hasImageMetrics
                   ? control.evaluation.imageMetrics : null
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
                            elide: Text.ElideRight
                            text: qsTr("精确率: %1").arg(control.percentage(model.precision, 1))
                        }
                    }

                    // 右侧：召回率饼图
                    ColumnLayout {
                        Layout.fillWidth: true
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
                            elide: Text.ElideRight
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
            visible: !control.evaluation || !control.evaluation.hasImageMetrics
            text: control.emptyText
            color: QuiColor.FontDark
        }
    }
}
