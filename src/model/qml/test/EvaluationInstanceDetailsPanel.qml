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
        QuiText { 
            text: qsTr("实例详情")
            font: QuiFont.Subtitle
        }
        QuiText {
            Layout.fillWidth: true
            text: control.evaluation && control.evaluation.selectedInstance.imageName
                  ? qsTr("图像：%1").arg(control.evaluation.selectedInstance.imageName)
                  : qsTr("请选择实例")
        }
        QuiText { Layout.fillWidth: true; text: control.evaluation ? qsTr("GT：%1").arg(control.evaluation.selectedInstance.gtClass || "—") : "" }
        QuiText { Layout.fillWidth: true; text: control.evaluation ? qsTr("PRED：%1").arg(control.evaluation.selectedInstance.predClass || "—") : "" }
        QuiText { Layout.fillWidth: true; text: control.evaluation ? qsTr("状态：%1").arg(control.evaluation.selectedInstance.statusText || control.evaluation.selectedInstance.status || "—") : "" }
        QuiText { Layout.fillWidth: true; text: control.evaluation ? qsTr("GT 实例：%1").arg(control.evaluation.selectedInstance.gtInstanceId || "—") : "" }
        QuiText { Layout.fillWidth: true; text: control.evaluation ? qsTr("PRED 实例：%1").arg(control.evaluation.selectedInstance.predInstanceId || "—") : "" }
        QuiText {
            Layout.fillWidth: true
            text: {
                if (!control.evaluation || !control.evaluation.selectedInstance.eventUuid)
                    return ""
                var selected = control.evaluation.selectedInstance
                var score = selected.score !== undefined && selected.score !== null
                            ? Number(selected.score).toFixed(3) : "—"
                var iou = selected.iou !== undefined && selected.iou !== null
                          ? Number(selected.iou).toFixed(3) : "—"
                return qsTr("Score：%1  IoU：%2").arg(score).arg(iou)
            }
        }
        Item { Layout.fillHeight: true }
    }
}
