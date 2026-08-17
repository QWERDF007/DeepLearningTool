import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

EvaluationMetricsPanelBase {
    id: control

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Repeater {
                model: control.evaluation && control.evaluation.hasInstanceMetrics
                       ? control.evaluation.instanceMetrics : null
                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    QuiText { text: model.label; font: QuiFont.BodyStrong }
                    QuiText { text: qsTr("精确率 %1").arg(model.precisionText) }
                    QuiText { text: qsTr("召回率 %1").arg(model.recallText) }
                    QuiText { text: qsTr("F1 %1").arg(model.f1Text) }
                    QuiText { text: qsTr("TP %1 · FP %2 · FN %3").arg(model.tp).arg(model.fp).arg(model.fn) }
                }
            }
            QuiText {
                visible: !control.evaluation || !control.evaluation.hasInstanceMetrics
                text: control.emptyText
                color: QuiColor.FontDark
            }
            Item { Layout.fillWidth: true }
        }
    }

    QuiContentDialog {
        id: classDialog
        width: 620
        title: qsTr("按类别实例指标")
        useNeutralButton: true
        useNegativeButton: false
        usePositiveButton: false
        neutralText: qsTr("关闭")
        contentDelegate: Component {
            ListView {
                implicitWidth: 560
                implicitHeight: 360
                clip: true
                headerPositioning: ListView.OverlayHeader
                header: RowLayout {
                    width: ListView.view.width
                    QuiComboBox {
                        id: classSort
                        model: [qsTr("F1"), qsTr("FP"), qsTr("FN"), qsTr("类别")]
                        onActivated: {
                            if (control.evaluation)
                                control.evaluation.sortedPerClassMetrics.sortBy(currentIndex === 1 ? "fp"
                                    : currentIndex === 2 ? "fn" : currentIndex === 3 ? "label" : "f1")
                        }
                    }
                }
                model: control.evaluation ? control.evaluation.sortedPerClassMetrics : null
                delegate: RowLayout {
                    width: ListView.view.width
                    spacing: 12
                    QuiText { Layout.fillWidth: true; text: model.label }
                    QuiText { text: model.precisionText }
                    QuiText { text: model.recallText }
                    QuiText { text: model.f1Text }
                    QuiText { text: qsTr("TP %1 / FP %2 / FN %3").arg(model.tp).arg(model.fp).arg(model.fn) }
                }
            }
        }
    }

    headerContent: QuiButton {
        anchors.fill: parent
        text: qsTr("按类别详情")
        enabled: !!control.evaluation && control.evaluation.perClassMetrics.count > 0
        onClicked: classDialog.open()
    }
}
