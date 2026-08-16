import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

Item {
    id: control
    property ModelEvaluationViewModel evaluation: null
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        QuiText { 
            text: qsTr("实例详情")
            font: QuiFont.Subtitle
        }

        GridLayout {
            Layout.fillWidth: true
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
                    color: control.anomalyDetection
                           ? (Number(control.selectedInstance.predClassId) === 1 ? "#e53935" : "#43a047")
                           : (control.selectedInstance.predClassColor || QuiColor.FontDark)
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
                text: control.anomalyDetection ? qsTr("分数") : qsTr("置信度 / IoU")
                color: QuiColor.FontDark
                Layout.alignment: Qt.AlignVCenter
            }
            QuiText {
                Layout.fillWidth: true
                text: {
                    if (!control.hasSelection)
                        return "—"
                    var score = Number(control.selectedInstance.score)
                    if (control.anomalyDetection)
                        return isFinite(score) ? score.toFixed(3) : "—"
                    var iou = Number(control.selectedInstance.iou)
                    var scoreText = isFinite(score) ? score.toFixed(3) : "—"
                    var iouText = isFinite(iou) ? Math.round(iou * 100) + "%" : "—"
                    return scoreText + " / " + iouText
                }
                elide: Text.ElideRight
            }
        }

        Item { Layout.fillHeight: true }
    }
}
