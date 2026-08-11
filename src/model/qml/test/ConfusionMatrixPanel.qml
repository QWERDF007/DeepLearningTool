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

    function isTotalKey(key) {
        return String(key) === "TOTAL"
    }

    function isCellSelected(rowKey, columnKey) {
        if (!control.evaluation)
            return false
        const row = control.evaluation.filteredInstances.matrixRow
        const column = control.evaluation.filteredInstances.matrixColumn
        /* 空矩阵筛选表示全部实例，默认选中右下角的 TOTAL/TOTAL 单元格。 */
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

    /**
     * @brief 安全读取混淆矩阵角色值。
     * @param row 行下标。
     * @param column 列下标。
     * @param role 角色编号。
     * @param fallback 模型不可用时的默认值。
     * @return 角色值或默认值。
     */
    function matrixData(row, column, role, fallback) {
        let matrix = control.evaluation && control.evaluation.hasConfusionMatrix
                     ? control.evaluation.confusionMatrix : null
        let sourceRow = typeof row === "number" ? Math.floor(row) : -1
        let sourceColumn = typeof column === "number" ? Math.floor(column) : -1
        if (!matrix || typeof matrix.rowCount !== "function"
                || typeof matrix.columnCount !== "function"
                || typeof matrix.index !== "function" || typeof matrix.data !== "function"
                || sourceRow < 0 || sourceColumn < 0
                || sourceRow >= matrix.rowCount() || sourceColumn >= matrix.columnCount())
            return fallback
        let value = matrix.data(matrix.index(sourceRow, sourceColumn), role)
        return value === undefined || value === null ? fallback : value
    }

    function matrixCellOptions(row, column, modelData) {
        if (typeof row !== "number" || typeof column !== "number")
            return ({ count: 0,
                      rowKey: "",
                      columnKey: "",
                      cellKind: EvaluationConfusionModel.CellKindNotApplicable,
                      selectable: false,
                      isDiagonal: false })
        const options = {
            count: control.matrixData(row, column, EvaluationConfusionModel.CountRole,
                                      modelData && modelData.display !== undefined ? modelData.display : 0),
            rowKey: String(control.matrixData(row, column, EvaluationConfusionModel.RowKeyRole,
                                              modelData && modelData.rowKey !== undefined ? modelData.rowKey : "")),
            columnKey: String(control.matrixData(row, column, EvaluationConfusionModel.ColumnKeyRole,
                                                 modelData && modelData.columnKey !== undefined
                                                 ? modelData.columnKey : "")),
            cellKind: control.matrixData(row, column, EvaluationConfusionModel.CellKindValueRole,
                                         EvaluationConfusionModel.CellKindNotApplicable),
            selectable: !!control.matrixData(row, column, EvaluationConfusionModel.SelectableRole,
                                             modelData && modelData.selectable !== undefined
                                             ? modelData.selectable : false),
            isDiagonal: !!control.matrixData(row, column, EvaluationConfusionModel.IsDiagonalRole,
                                             modelData && modelData.isDiagonal !== undefined
                                             ? modelData.isDiagonal : false)
        }
        return options
    }

    function rebuildMatrix() {
        let matrix = control.evaluation && control.evaluation.hasConfusionMatrix
                     ? control.evaluation.confusionMatrix : null
        let columnSource = []
        let classColumnLabels = []
        let classRowLabels = []
        let rowCount = 0
        if (matrix) {
            let columnCount = matrix.columnCount()
            rowCount = matrix.rowCount()
            for (let c = 0; c < columnCount; ++c) {
                let key = control.matrixData(0, c, EvaluationConfusionModel.ColumnKeyRole, "")
                let label = control.matrixData(0, c, EvaluationConfusionModel.ColumnLabelRole, "")
                let specialColumn = String(key) === "FP" || String(key) === "TOTAL"
                columnSource.push({
                    title: specialColumn
                           ? confusionTable.customItem(com_matrix_header, {
                               label: label,
                               isTotal: control.isTotalKey(key)
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
                let rowKey = control.matrixData(r, 0, EvaluationConfusionModel.RowKeyRole, "")
                let rowLabel = control.matrixData(r, 0, EvaluationConfusionModel.RowLabelRole, "")
                if (String(rowKey) !== "FN" && String(rowKey) !== "TOTAL")
                    classRowLabels.push({
                        row: r,
                        label: String(rowLabel)
                    })
            }
        }
        control.classColumnLabels = classColumnLabels
        control.classRowLabels = classRowLabels
        control.matrixRowCount = rowCount
        /*
         * 直接模型模式下只更新列布局和标签元数据，单元格仍由
         * EvaluationConfusionModel 的角色数据驱动，不再复制成 JS 行数组。
         */
        const schemaChanged = !!matrix && !sameColumnSchema(confusionTable.columnSource, columnSource)
        if (schemaChanged)
            confusionTable.columnSource = columnSource
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

    onEvaluationChanged: {
        rebuildMatrix()
    }

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
    }

    Component {
        id: com_matrix_header
        Item {
            property var options: ({ label: "", isTotal: false })
            QuiTextIcon {
                anchors.centerIn: parent
                visible: !!options.isTotal
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
            property var options: ({ count: 0,
                                     rowKey: "",
                                     columnKey: "",
                                     cellKind: EvaluationConfusionModel.CellKindNotApplicable,
                                     selectable: false,
                                     isDiagonal: false })
            property bool selected: control.isCellSelected(options.rowKey, options.columnKey)
            property bool totalCell: options.cellKind === EvaluationConfusionModel.CellKindAll
                                     || options.cellKind === EvaluationConfusionModel.CellKindPredTotal
                                     || options.cellKind === EvaluationConfusionModel.CellKindGtTotal
                                     || options.cellKind === EvaluationConfusionModel.CellKindFalsePositiveTotal
                                     || options.cellKind === EvaluationConfusionModel.CellKindFalseNegativeTotal
            readonly property color baseColor: totalCell
                                               || (options.cellKind === EvaluationConfusionModel.CellKindMatch
                                                   && options.isDiagonal)
                                               ? QuiColor.Highlight
                                               : (options.cellKind === EvaluationConfusionModel.CellKindFalsePositive
                                                  || options.cellKind === EvaluationConfusionModel.CellKindFalseNegative
                                                  ? QuiColor.Background : QuiColor.Primary)
            color: selected ? Qt.darker(baseColor) : baseColor
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
                    // Dynamic table delegates can be created before their options are injected.
                    text: options && options.count !== undefined && options.count !== null
                          ? String(options.count) : "0"
                    color: QuiColor.FontPrimary
                }
            }
            MouseArea {
                id: cellMouse
                anchors.fill: parent
                enabled: !!(options && options.selectable)
                hoverEnabled: enabled
                onClicked: {
                    if (!control.evaluation || !options
                            || options.rowKey === undefined || options.columnKey === undefined)
                        return
                    control.evaluation.selectMatrixCell(options.rowKey, options.columnKey)
                }
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
                visible: control.matrixRowCount > 0
                externalModel: control.evaluation && control.evaluation.hasConfusionMatrix
                              ? control.evaluation.confusionMatrix : null
                externalCellDelegate: com_matrix_cell
                externalCellOptionsProvider: function(row, column, modelData) {
                    return control.matrixCellOptions(row, column, modelData)
                }
                externalCellClickHandler: function(row, column, options) {
                    if (!control.evaluation || !options || !options.selectable)
                        return
                    control.evaluation.selectMatrixCell(options.rowKey, options.columnKey)
                }
                verticalHeaderDelegate: Component {
                    Rectangle {
                        property int sourceRow: typeof row === "number"
                                                ? row
                                                : (typeof index === "number" ? index : -1)
                        property string label: String(control.matrixData(
                            sourceRow, 0, EvaluationConfusionModel.RowLabelRole, ""))
                        property string rowKey: String(control.matrixData(
                            sourceRow, 0, EvaluationConfusionModel.RowKeyRole, ""))
                        property bool classRow: rowKey !== "FN" && rowKey !== "TOTAL"
                        implicitWidth: control.leftAxisWidth
                        implicitHeight: confusionTable.currentRowHeight(sourceRow)
                        color: confusionTable.headerColor
                        border.color: confusionTable.borderColor
                        border.width: 1

                        QuiTextIcon {
                            anchors.centerIn: parent
                            visible: parent.rowKey === "TOTAL"
                            iconSource: QuiFontIcon.Picture
                            iconColor: confusionTable.headerTextColor
                        }
                        QuiText {
                            id: rowLabel
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            visible: parent.rowKey !== "TOTAL"
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
                x: confusionTable.viewportX
                y: confusionTable.frameY
                width: Math.max(0, confusionTable.viewportWidth - confusionTable.frozenWidth)
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
                x: confusionTable.frameX
                y: confusionTable.viewportY
                width: confusionTable.verticalHeaderWidth
                height: Math.min(confusionTable.viewportHeight,
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
                x: confusionTable.frameX + confusionTable.frameWidth
                y: confusionTable.viewportY
                width: control.rightLabelsWidth
                height: confusionTable.viewportHeight
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
                x: confusionTable.viewportX
                y: confusionTable.frameY + confusionTable.frameHeight
                width: Math.max(0, confusionTable.viewportWidth - confusionTable.frozenWidth)
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
