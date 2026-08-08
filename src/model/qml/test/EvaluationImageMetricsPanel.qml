import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

Rectangle {
    id: control
    clip: true
    color: QuiColor.Primary
    property ModelEvaluationViewModel evaluation: null

    function percentage(value, decimals) {
        var number = Number(value)
        if (!isFinite(number))
            number = 0
        var text = (Math.max(0, Math.min(1, number)) * 100).toFixed(decimals)
        if (decimals > 0)
            text = text.replace(/\.?(0+)$/, "")
        return text + "%"
    }

    function pieData(value) {
        var number = Number(value)
        if (!isFinite(number))
            number = 0
        number = Math.max(0, Math.min(1, number)) * 100
        return ({
            labels: [qsTr("正确"), qsTr("错误")],
            datasets: [{
                data: [number, 100 - number],
                backgroundColor: ["#00b85a", "#d71920"],
                borderColor: ["#e8e8e8", "#e8e8e8"],
                borderWidth: 1
            }]
        })
    }

    function pieOptions() {
        return ({
            maintainAspectRatio: false,
            responsive: true,
            legend: { display: false },
            tooltips: { enabled: false },
            animation: { duration: 0 }
        })
    }

    ColumnLayout {
        anchors.fill: parent
        QuiText { 
            text: qsTr("图像统计")
            font: QuiFont.Subtitle
        }

        Repeater {
            model: control.evaluation && control.evaluation.hasImageMetrics
                   ? control.evaluation.imageMetrics : null
            delegate: ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                QuiText {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("F1-Score: %1").arg(control.percentage(model.f1, 2))
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                QuiChart {
                                    anchors.centerIn: parent
                                    width: Math.min(parent.width, parent.height)
                                    height: width
                                    animationDuration: 0
                                    chartType: "pie"
                                    chartData: control.pieData(model.precision)
                                    chartOptions: control.pieOptions()
                                }
                            }

                            QuiText {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: qsTr("精确率: %1").arg(control.percentage(model.precision, 1))
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                QuiChart {
                                    anchors.centerIn: parent
                                    width: Math.min(parent.width, parent.height)
                                    height: width
                                    animationDuration: 0
                                    chartType: "pie"
                                    chartData: control.pieData(model.recall)
                                    chartOptions: control.pieOptions()
                                }
                            }

                            QuiText {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: qsTr("召回率: %1").arg(control.percentage(model.recall, 1))
                            }
                        }
                    }
                }
            }
        }
        QuiText {
            Layout.fillWidth: true
            visible: !control.evaluation || !control.evaluation.hasImageMetrics
            text: qsTr("当前方法没有图像指标")
            color: QuiColor.FontDark
        }
        Item { Layout.fillHeight: true }
    }
}
