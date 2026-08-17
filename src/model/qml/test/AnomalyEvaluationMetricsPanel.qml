import QtQuick
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

/*
 * 异常检测没有实例级指标：该面板保持占位，图像级指标由
 * AnomalyEvaluationImageMetricsPanel 展示。
 */
EvaluationMetricsPanelBase {
    id: control
    emptyText: qsTr("异常检测使用图像级指标")

    ColumnLayout {
        anchors.fill: parent
        QuiText {
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            visible: !control.evaluation || !control.evaluation.hasInstanceMetrics
            text: control.emptyText
            color: QuiColor.FontDark
        }
        Item { Layout.fillHeight: true }
    }
}
