import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

Item {
    id: control
    property ModelEvaluationViewModel evaluation: null

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            QuiText { text: qsTr("方法图表"); font: QuiFont.Title }
            QuiComboBox {
                id: chartSelector
                Layout.fillWidth: true
                visible: count > 1
                model: control.evaluation ? control.evaluation.charts : null
                textRole: "title"
                currentIndex: 0
            }
        }
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: !!control.evaluation && control.evaluation.charts.count > 0
            sourceComponent: ColumnLayout {
                spacing: 4
                QuiChart {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    property var descriptor: control.evaluation && chartSelector.currentIndex >= 0
                                             ? control.evaluation.charts.descriptor(chartSelector.currentIndex) : ({})
                    chartType: descriptor.kind || "line"
                    chartData: descriptor.data || ({labels: [], datasets: []})
                    chartOptions: descriptor.options || ({maintainAspectRatio: false})
                }
            }
        }
        QuiText {
            visible: !control.evaluation || control.evaluation.charts.count === 0
            text: qsTr("当前方法没有可用图表")
            color: QuiColor.FontDark
        }
    }
}
