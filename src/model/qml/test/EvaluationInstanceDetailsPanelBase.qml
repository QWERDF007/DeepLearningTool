import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

/*
 * 实例详情面板 Base：
 * - 提供字段布局、状态徽标与公共选择逻辑。
 * - 方法子类覆盖 metricLabel / formatMetric / predictedClassColor。
 */
Rectangle {
    id: control
    color: QuiColor.Primary
    property ModelEvaluationViewModel evaluation: null
    property string title: qsTr("实例详情")
    property string metricLabel: qsTr("置信度 / IoU")
    readonly property var selectedInstance: evaluation ? evaluation.selectedInstance : ({})
    readonly property bool hasSelection: !!selectedInstance
                                                && String(selectedInstance.eventUuid || "").length > 0
    readonly property bool anomalyDetection: !!evaluation && evaluation.anomalyDetection

    function displayValue(value) {
        if (value === undefined || value === null)
            return "—"
        var text = String(value)
        return text.length > 0 ? text : "—"
    }

    function formatMetric(instance) {
        var score = Number(instance.score)
        var iou = Number(instance.iou)
        var scoreText = isFinite(score) ? score.toFixed(3) : "—"
        var iouText = isFinite(iou) ? Math.round(iou * 100) + "%" : "—"
        return scoreText + " / " + iouText
    }

    function predictedClassColor(instance) {
        return instance.predClassColor || QuiColor.FontDark
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // 顶栏 Header 容器（包含标题）
        RowLayout {
            id: headerHost
            Layout.fillWidth: true

            QuiText {
                text: control.title
                font: QuiFont.Subtitle
            }

            Item { Layout.fillWidth: true }
        }

        Item {
            id: contentHost
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                columns: 2
                columnSpacing: 16
                rowSpacing: 12

                QuiText {
                    text: qsTr("图像名称")
                    color: QuiColor.FontDark
                    Layout.alignment: Qt.AlignTop
                }
                QuiText {
                    Layout.fillWidth: true
                    text: control.hasSelection ? control.displayValue(control.selectedInstance.imageName)
                                                : qsTr("请选择实例")
                    elide: Text.ElideRight
                }

                QuiText {
                    text: qsTr("GT 类别")
                    color: QuiColor.FontDark
                    Layout.alignment: Qt.AlignVCenter
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Rectangle {
                        visible: control.hasSelection
                                 && String(control.selectedInstance.gtClassColor || "").length > 0
                        Layout.preferredWidth: 10
                        Layout.preferredHeight: 10
                        radius: 2
                        color: control.selectedInstance.gtClassColor || QuiColor.FontDark
                    }
                    QuiText {
                        Layout.fillWidth: true
                        text: control.hasSelection ? control.displayValue(control.selectedInstance.gtClass)
                                                    : "—"
                        elide: Text.ElideRight
                    }
                }

                QuiText {
                    text: qsTr("预测类别")
                    color: QuiColor.FontDark
                    Layout.alignment: Qt.AlignVCenter
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Rectangle {
                        visible: control.hasSelection
                        Layout.preferredWidth: 10
                        Layout.preferredHeight: 10
                        radius: 2
                        color: control.predictedClassColor(control.selectedInstance)
                    }
                    QuiText {
                        Layout.fillWidth: true
                        text: control.hasSelection ? control.displayValue(control.selectedInstance.predClass)
                                                    : "—"
                        elide: Text.ElideRight
                    }
                }

                QuiText {
                    text: qsTr("预测状态")
                    color: QuiColor.FontDark
                    Layout.alignment: Qt.AlignVCenter
                }
                Rectangle {
                    id: statusBadgeContainer
                    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                    implicitWidth: statusBadge.implicitWidth + 10
                    implicitHeight: statusBadge.implicitHeight + 4
                    radius: 3
                    color: {
                        if (!control.hasSelection)
                            return QuiColor.Transparent
                        var statusKind = Number(control.selectedInstance.statusKind)
                        if (statusKind === EvaluationInstanceModel.StatusTruePositive
                                || statusKind === EvaluationInstanceModel.StatusTrueNegative)
                            return "#43a047"
                        if (statusKind === EvaluationInstanceModel.StatusClassMismatch)
                            return "#fb8c00"
                        return "#e53935"
                    }

                    QuiText {
                        id: statusBadge
                        anchors.centerIn: parent
                        text: control.hasSelection
                              ? control.displayValue(control.selectedInstance.statusText
                                                     || control.selectedInstance.status)
                              : "—"
                        color: control.hasSelection ? "white" : QuiColor.FontPrimary
                        font: QuiFont.Body
                        elide: Text.ElideRight
                    }
                }

                QuiText {
                    text: control.metricLabel
                    color: QuiColor.FontDark
                    Layout.alignment: Qt.AlignVCenter
                }
                QuiText {
                    Layout.fillWidth: true
                    text: {
                        if (!control.hasSelection)
                            return "—"
                        return control.formatMetric(control.selectedInstance)
                    }
                    elide: Text.ElideRight
                }
            }
        }
    }
}
