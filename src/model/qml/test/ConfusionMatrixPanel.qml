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
        RowLayout {
            Layout.fillWidth: true
            QuiText { text: qsTr("混淆矩阵（行=PRED，列=GT）"); font: QuiFont.Title }
            Item { Layout.fillWidth: true }
            QuiButton {
                text: qsTr("显示全部")
                enabled: !!control.evaluation
                onClicked: control.evaluation.clearMatrixSelection()
            }
        }
        HorizontalHeaderView {
            Layout.fillWidth: true
            syncView: confusionGrid
            clip: true
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            VerticalHeaderView {
                Layout.fillHeight: true
                syncView: confusionGrid
                clip: true
            }
            TableView {
                id: confusionGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: control.evaluation && control.evaluation.hasConfusionMatrix
                       ? control.evaluation.confusionMatrix : null
                delegate: Rectangle {
                    implicitWidth: 120
                    implicitHeight: 48
                    property bool selected: !!control.evaluation
                                          && control.evaluation.filteredInstances.matrixRow === model.rowKey
                                          && control.evaluation.filteredInstances.matrixColumn === model.columnKey
                    color: selected
                           ? QuiColor.Highlight
                           : (model.cellKind === "match" && model.isDiagonal
                           ? "#2e7d32"
                           : (model.isError ? "#c62828" : QuiColor.Background))
                    opacity: model.count > 0 || model.cellKind === "all" ? 1.0 : 0.72
                    border.color: model.cellKind === "all" || model.cellKind === "pred_total"
                                  || model.cellKind === "gt_total" ? QuiColor.FontDark : QuiColor.Border
                    Column {
                        anchors.centerIn: parent
                        spacing: 1
                        QuiText { anchors.horizontalCenter: parent.horizontalCenter; text: model.count }
                        QuiText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("%1 / %2").arg(model.rowLabel).arg(model.columnLabel)
                            font.pixelSize: 9
                            color: QuiColor.FontDark
                        }
                    }
                    MouseArea {
                        id: cellMouse
                        anchors.fill: parent
                        enabled: model.selectable
                        hoverEnabled: enabled
                        onClicked: control.evaluation.selectMatrixCell(model.rowKey, model.columnKey)
                    }
                    QuiToolTip {
                        visible: cellMouse.containsMouse
                        text: model.tooltip || qsTr("PRED %1 / GT %2").arg(model.rowLabel).arg(model.columnLabel)
                    }
                }
            }
            QuiText {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !control.evaluation || !control.evaluation.hasConfusionMatrix
                text: qsTr("当前方法没有混淆矩阵")
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: QuiColor.FontDark
            }
        }
    }
}
