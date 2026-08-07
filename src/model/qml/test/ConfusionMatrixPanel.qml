import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

Item {
    id: control
    property ModelEvaluationViewModel evaluation: null
    property var classColumnLabels: []
    property var classRowLabels: []
    property int matrixRowCount: 0
    readonly property real leftAxisWidth: 48
    readonly property real rightLabelsWidth: 104
    readonly property real bottomLabelsHeight: 72

    function isTotalLabel(label) {
        return label === qsTr("合计") || label === "TOTAL"
    }

    function predictedClassLabel(rowKey, fallback) {
        if (String(rowKey) === "0")
            return qsTr("正常")
        if (String(rowKey) === "1")
            return qsTr("异常")
        return fallback
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

    function sameColumnSchema(left, right) {
        if (!left || left.length !== right.length)
            return false
        for (let i = 0; i < right.length; ++i) {
            if (String(left[i].dataIndex) !== String(right[i].dataIndex))
                return false
        }
        return true
    }

    function rebuildMatrix() {
        let matrix = control.evaluation && control.evaluation.hasConfusionMatrix
                     ? control.evaluation.confusionMatrix : null
        let columnSource = []
        let rows = []
        let classColumnLabels = []
        let classRowLabels = []
        if (matrix) {
            let columnCount = matrix.columnCount()
            let rowCount = matrix.rowCount()
            let columnKeys = []
            for (let c = 0; c < columnCount; ++c) {
                let key = matrix.data(matrix.index(0, c), EvaluationConfusionModel.ColumnKeyRole)
                let label = matrix.data(matrix.index(0, c), EvaluationConfusionModel.ColumnLabelRole)
                let specialColumn = String(key) === "FP" || String(key) === "TOTAL"
                columnKeys.push(key)
                columnSource.push({
                    title: specialColumn
                           ? confusionTable.customItem(com_matrix_header, {
                               label: label,
                               isTotal: control.isTotalLabel(label)
                           }) : "",
                    dataIndex: key,
                    width: 32,
                    minimumWidth: 32,
                    stretch: false,
                    frozen: String(key) === "FP" || String(key) === "TOTAL"
                })
                if (!specialColumn)
                    classColumnLabels.push({ column: c, label: String(label) })
            }
            for (let r = 0; r < rowCount; ++r) {
                let row = {}
                for (let c = 0; c < columnCount; ++c) {
                    let idx = matrix.index(r, c)
                    row[columnKeys[c]] = confusionTable.customItem(com_matrix_cell, {
                        count: matrix.data(idx, EvaluationConfusionModel.CountRole),
                        rowKey: matrix.data(idx, EvaluationConfusionModel.RowKeyRole),
                        columnKey: matrix.data(idx, EvaluationConfusionModel.ColumnKeyRole),
                        cellKind: matrix.data(idx, EvaluationConfusionModel.CellKindValueRole),
                        selectable: matrix.data(idx, EvaluationConfusionModel.SelectableRole),
                        isDiagonal: matrix.data(idx, EvaluationConfusionModel.IsDiagonalRole)
                    })
                }
                row.__rowKey = matrix.data(matrix.index(r, 0), EvaluationConfusionModel.RowKeyRole)
                row.__rowLabel = matrix.data(matrix.index(r, 0), EvaluationConfusionModel.RowLabelRole)
                if (String(row.__rowKey) !== "FN" && String(row.__rowKey) !== "TOTAL")
                    classRowLabels.push({
                        row: r,
                        label: control.predictedClassLabel(row.__rowKey, String(row.__rowLabel))
                    })
                rows.push(row)
            }
        }
        control.classColumnLabels = classColumnLabels
        control.classRowLabels = classRowLabels
        control.matrixRowCount = rows.length
        if (!sameColumnSchema(confusionTable.columnSource, columnSource))
            confusionTable.columnSource = columnSource
        confusionTable.dataSource = rows
    }

    function matrixRowsHeight() {
        if (matrixRowCount <= 0)
            return 0
        return matrixRowCount * confusionTable.rowHeight
               + Math.max(0, matrixRowCount - 1) * confusionTable.rowSpacing
    }

    function classColumnsWidth() {
        let width = 0
        for (let i = 0; i < classColumnLabels.length; ++i) {
            width += confusionTable.columnWidth(classColumnLabels[i].column)
            if (i + 1 < classColumnLabels.length)
                width += confusionTable.columnSpacing
        }
        return width
    }

    function matrixColumnsWidth() {
        let count = confusionTable.columnSource ? confusionTable.columnSource.length : 0
        let width = 0
        for (let column = 0; column < count; ++column)
            width += confusionTable.columnWidth(column)
        if (count > 1)
            width += (count - 1) * confusionTable.columnSpacing
        return width
    }

    function rowVisualY(row) {
        return row * (confusionTable.rowHeight + confusionTable.rowSpacing)
               - confusionTable.contentY
    }

    onEvaluationChanged: rebuildMatrix()

    Connections {
        target: control.evaluation ? control.evaluation.confusionMatrix : null
        function onModelReset() {
            control.rebuildMatrix()
        }
        function onRowsInserted(parent, first, last) {
            control.rebuildMatrix()
        }
        function onRowsRemoved(parent, first, last) {
            control.rebuildMatrix()
        }
        function onColumnsInserted(parent, first, last) {
            control.rebuildMatrix()
        }
        function onColumnsRemoved(parent, first, last) {
            control.rebuildMatrix()
        }
        function onDataChanged(topLeft, bottomRight, roles) {
            control.rebuildMatrix()
        }
    }

    Component {
        id: com_matrix_header
        Item {
            QuiTextIcon {
                anchors.centerIn: parent
                visible: options.isTotal
                iconSource: QuiFontIcon.Picture
                iconColor: confusionTable.headerTextColor
            }
            QuiText {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                visible: !options.isTotal
                text: String(options.label || "")
                color: confusionTable.headerTextColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }

    Component {
        id: com_matrix_cell
        Rectangle {
            property bool selected: control.isCellSelected(options.rowKey, options.columnKey)
            property bool totalCell: options.cellKind === EvaluationConfusionModel.CellKindAll
                                     || options.cellKind === EvaluationConfusionModel.CellKindPredTotal
                                     || options.cellKind === EvaluationConfusionModel.CellKindGtTotal
                                     || options.cellKind === EvaluationConfusionModel.CellKindFalsePositiveTotal
                                     || options.cellKind === EvaluationConfusionModel.CellKindFalseNegativeTotal
            color: selected
                   ? Qt.darker(QuiColor.Highlight)
                   : (totalCell
                      || (options.cellKind === EvaluationConfusionModel.CellKindMatch && options.isDiagonal)
                      ? QuiColor.Highlight
                      : (options.cellKind === EvaluationConfusionModel.CellKindFalsePositive
                         || options.cellKind === EvaluationConfusionModel.CellKindFalseNegative
                         ? QuiColor.Background : QuiColor.Primary))
            opacity: selected || options.count > 0
                     || options.cellKind === EvaluationConfusionModel.CellKindAll ? 1.0 : 0.72
            border.color: selected
                          ? QuiColor.Highlight
                          : (totalCell ? QuiColor.FontDark : QuiColor.Border)
            border.width: selected ? 2 : 1

            Column {
                anchors.centerIn: parent
                QuiText {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: options.count
                    color: QuiColor.FontPrimary
                }
            }
            MouseArea {
                id: cellMouse
                anchors.fill: parent
                enabled: options.selectable
                hoverEnabled: enabled
                onClicked: control.evaluation.selectMatrixCell(options.rowKey, options.columnKey)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent

        QuiText {
            Layout.fillWidth: true
            text: qsTr("混淆矩阵")
            font: QuiFont.Subtitle
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            QuiTableView {
                id: confusionTable
                anchors.left: parent.left
                anchors.top: parent.top
                width: Math.min(Math.max(0, parent.width - control.rightLabelsWidth),
                                control.leftAxisWidth + control.matrixColumnsWidth())
                height: Math.max(0, Math.min(parent.height - control.bottomLabelsHeight,
                                             headerHeight + control.matrixRowsHeight()))
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
                rowSelectionEnabled: false
                hoverEnabled: false
                zebraEnabled: false
                resizableColumns: false

                verticalHeaderDelegate: Component {
                    Rectangle {
                        property int sourceRow: typeof row === "undefined" ? index : row
                        property string label: {
                            let rowData = confusionTable.getRow(sourceRow)
                            return rowData && rowData.__rowLabel !== undefined
                                   ? String(rowData.__rowLabel) : ""
                        }
                        property bool classRow: String(label) !== "FN"
                                                && !control.isTotalLabel(label)
                        implicitWidth: control.leftAxisWidth
                        implicitHeight: confusionTable.currentRowHeight(sourceRow)
                        color: confusionTable.headerColor
                        border.color: confusionTable.borderColor
                        border.width: 1

                        QuiTextIcon {
                            anchors.centerIn: parent
                            visible: control.isTotalLabel(parent.label)
                            iconSource: QuiFontIcon.Picture
                            iconColor: confusionTable.headerTextColor
                        }
                        QuiText {
                            id: rowLabel
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            visible: !control.isTotalLabel(parent.label)
                            text: parent.classRow ? "" : parent.label
                            color: confusionTable.headerTextColor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Item {
                id: groundTruthViewport
                x: confusionTable.view.x
                y: confusionTable.y
                width: Math.max(0, confusionTable.view.width - confusionTable.frozenWidth)
                height: confusionTable.headerHeight
                visible: control.classColumnLabels.length > 0
                clip: true
                z: 30

                Rectangle {
                    x: -confusionTable.contentX
                    width: control.classColumnsWidth()
                    height: parent.height
                    color: confusionTable.headerColor
                    border.color: confusionTable.borderColor
                    border.width: 1

                    QuiText {
                        anchors.centerIn: parent
                        text: "GROUND TRUTH"
                        color: confusionTable.headerTextColor
                        font.bold: true
                    }
                }
            }

            Item {
                id: predictedViewport
                x: confusionTable.x
                y: confusionTable.view.y
                width: confusionTable.verticalHeader.width
                height: Math.min(confusionTable.view.height,
                                 control.classRowLabels.length
                                 * (confusionTable.rowHeight + confusionTable.rowSpacing))
                visible: control.classRowLabels.length > 0
                clip: true
                z: 30

                Rectangle {
                    y: -confusionTable.contentY
                    width: parent.width
                    height: control.classRowLabels.length * confusionTable.rowHeight
                            + Math.max(0, control.classRowLabels.length - 1)
                              * confusionTable.rowSpacing
                    color: confusionTable.headerColor
                    border.color: confusionTable.borderColor
                    border.width: 1

                    QuiText {
                        anchors.centerIn: parent
                        text: "PREDICTED"
                        color: confusionTable.headerTextColor
                        font.bold: true
                        rotation: -90
                    }
                }
            }

            Item {
                id: rightClassLabels
                x: confusionTable.x + confusionTable.width
                y: confusionTable.view.y
                width: control.rightLabelsWidth
                height: confusionTable.view.height
                clip: true

                Repeater {
                    model: control.classRowLabels

                    Rectangle {
                        required property var modelData
                        x: 0
                        y: control.rowVisualY(modelData.row)
                        width: rightClassLabels.width
                        height: confusionTable.rowHeight
                        color: confusionTable.headerColor

                        QuiText {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            text: modelData.label
                            color: confusionTable.headerTextColor
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Item {
                id: bottomClassLabels
                x: confusionTable.view.x
                y: confusionTable.y + confusionTable.height
                width: Math.max(0, confusionTable.view.width - confusionTable.frozenWidth)
                height: control.bottomLabelsHeight
                clip: true

                Repeater {
                    model: control.classColumnLabels

                    Item {
                        required property var modelData
                        x: confusionTable.columnOffset(modelData.column) - confusionTable.contentX
                        width: confusionTable.columnWidth(modelData.column)
                        height: bottomClassLabels.height

                        QuiText {
                            x: parent.width / 2
                            y: 4
                            width: bottomClassLabels.height - 4
                            text: modelData.label
                            color: confusionTable.headerTextColor
                            elide: Text.ElideRight
                            transformOrigin: Item.TopLeft
                            rotation: 45
                        }
                    }
                }
            }
        }
    }
}
