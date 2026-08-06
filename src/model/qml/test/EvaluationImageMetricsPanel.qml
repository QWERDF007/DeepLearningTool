import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

Rectangle {
    id: control
    color: QuiColor.Primary
    property ModelEvaluationViewModel evaluation: null

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        QuiText { 
            text: qsTr("图像统计")
            font: QuiFont.Subtitle
        }
        Repeater {
            model: control.evaluation && control.evaluation.hasImageMetrics
                   ? control.evaluation.imageMetrics : null
            delegate: ColumnLayout {
                Layout.fillWidth: true
                QuiText { text: model.label; font: QuiFont.BodyStrong }
                QuiText { text: qsTr("精确率 %1").arg(model.precisionText) }
                QuiText { text: qsTr("召回率 %1").arg(model.recallText) }
                QuiText { text: qsTr("F1 %1").arg(model.f1Text) }
                QuiText {
                    text: qsTr("TP %1 · FP %2 · FN %3").arg(model.tp).arg(model.fp).arg(model.fn)
                    MouseArea { id: definitionMouse; anchors.fill: parent; hoverEnabled: true }
                    QuiToolTip {
                        visible: definitionMouse.containsMouse
                        text: control.evaluation && control.evaluation.imageMetricDefinition
                              ? qsTr("样本单位：%1\n聚合方式：%2")
                                .arg(control.evaluation.imageMetricDefinition.sample_unit || "—")
                                .arg(control.evaluation.imageMetricDefinition.aggregation || "—")
                              : qsTr("按图像统计")
                    }
                }
            }
        }
        QuiText {
            visible: !control.evaluation || !control.evaluation.hasImageMetrics
            text: qsTr("当前方法没有图像指标")
            color: QuiColor.FontDark
        }
        Item { Layout.fillHeight: true }
    }
}
