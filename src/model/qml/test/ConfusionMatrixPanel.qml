import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

Item {
    id: control
    property ModelEvaluationViewModel evaluation: null

    function displayValue(value, modelObject) {
        if (value && typeof value === "object" && value.display !== undefined)
            return String(value.display)
        if (confusionTable.headerTextRole && value && typeof value === "object"
                && value[confusionTable.headerTextRole] !== undefined)
            return String(value[confusionTable.headerTextRole])
        if (modelObject && typeof modelObject === "object" && modelObject.display !== undefined)
            return String(modelObject.display)
        if (confusionTable.headerTextRole && modelObject && typeof modelObject === "object"
                && modelObject[confusionTable.headerTextRole] !== undefined)
            return String(modelObject[confusionTable.headerTextRole])
        return value === undefined || value === null ? "" : String(value)
    }

    function headerLabel(modelDataValue, displayValueValue, modelObject) {
        let value = modelDataValue
        if (value === undefined)
            value = displayValueValue
        return displayValue(value, modelObject)
    }

    function isTotalLabel(label) {
        return label === qsTr("合计") || label === "TOTAL"
    }

    function isCellSelected(rowKey, columnKey) {
        if (!control.evaluation)
            return false
        const row = control.evaluation.filteredInstances.matrixRow
        const column = control.evaluation.filteredInstances.matrixColumn
        // An empty matrix filter means all instances.  Keep that state visible
        // by selecting the bottom-right TOTAL/TOTAL cell by default.
        if (!row && !column)
            return rowKey === "TOTAL" && columnKey === "TOTAL"
        return row === rowKey && column === columnKey
    }

    ColumnLayout {
        anchors.fill: parent

        QuiText {
            Layout.fillWidth: true
            text: qsTr("混淆矩阵")
            font: QuiFont.Title
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            QuiTableView {
                id: confusionTable
                anchors.fill: parent
                model: control.evaluation && control.evaluation.hasConfusionMatrix
                       ? control.evaluation.confusionMatrix : null
                verticalHeaderVisible: true
                headerHeight: 36
                rowHeight: 32
                defaultColumnWidth: 32
                minimumColumnWidth: 32
                columnSpacing: 1
                rowSpacing: 1
                headerColor: QuiColor.Background
                headerTextColor: QuiColor.FontPrimary
                borderColor: QuiColor.Border
                showGridLines: true

                headerDelegate: Component {
                    Rectangle {
                        property int sourceColumn: typeof column === "undefined" ? index : column
                        property string label: control.headerLabel(
                            typeof modelData === "undefined" ? undefined : modelData,
                            typeof display === "undefined" ? undefined : display,
                            model)
                        implicitWidth: confusionTable.columnWidth(sourceColumn)
                        implicitHeight: confusionTable.headerHeight
                        color: confusionTable.headerColor
                        border.color: confusionTable.borderColor
                        border.width: 1

                        QuiTextIcon {
                            anchors.centerIn: parent
                            visible: control.isTotalLabel(parent.label)
                            iconSource: QuiFontIcon.Calculator
                            iconColor: confusionTable.headerTextColor
                            iconSize: 18
                        }
                        QuiText {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            visible: !control.isTotalLabel(parent.label)
                            text: parent.label
                            color: confusionTable.headerTextColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }

                verticalHeaderDelegate: Component {
                    Rectangle {
                        property int sourceRow: typeof row === "undefined" ? index : row
                        property string label: control.headerLabel(
                            typeof modelData === "undefined" ? undefined : modelData,
                            typeof display === "undefined" ? undefined : display,
                            model)
                        implicitWidth: Math.max(72, rowLabel.implicitWidth + 16)
                        implicitHeight: confusionTable.currentRowHeight(sourceRow)
                        color: confusionTable.headerColor
                        border.color: confusionTable.borderColor
                        border.width: 1

                        QuiTextIcon {
                            anchors.centerIn: parent
                            visible: control.isTotalLabel(parent.label)
                            iconSource: QuiFontIcon.Calculator
                            iconColor: confusionTable.headerTextColor
                            iconSize: 18
                        }
                        QuiText {
                            id: rowLabel
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            visible: !control.isTotalLabel(parent.label)
                            text: parent.label
                            color: confusionTable.headerTextColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }

                delegate: Rectangle {
                    implicitWidth: confusionTable.columnWidth(column)
                    implicitHeight: confusionTable.rowHeight
                    property bool selected: control.isCellSelected(model.rowKey, model.columnKey)
                    color: selected
                           ? QuiColor.Highlight
                           : (model.cellKindValue === EvaluationConfusionModel.CellKindMatch && model.isDiagonal
                           ? "#2e7d32"
                           : (model.isError ? "#c62828" : QuiColor.Primary))
                    opacity: selected || model.count > 0
                             || model.cellKindValue === EvaluationConfusionModel.CellKindAll ? 1.0 : 0.72
                    border.color: selected
                                  ? QuiColor.Highlight
                                  : (model.cellKindValue === EvaluationConfusionModel.CellKindAll
                                     || model.cellKindValue === EvaluationConfusionModel.CellKindPredTotal
                                     || model.cellKindValue === EvaluationConfusionModel.CellKindGtTotal
                                     ? QuiColor.FontDark : QuiColor.Border)
                    border.width: selected ? 2 : 1

                    Column {
                        anchors.centerIn: parent
                        QuiText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: model.count
                            color: QuiColor.FontPrimary
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
                        text: model.tooltip || qsTr("预测 %1 / Ground Truth %2").arg(model.rowLabel).arg(model.columnLabel)
                    }
                }
            }
        }
    }
}
