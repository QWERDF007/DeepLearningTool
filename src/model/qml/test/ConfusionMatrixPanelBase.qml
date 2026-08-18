import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

/*
 * 混淆矩阵面板 Base：
 * - 收拢矩阵模型读取、列结构重建、单元格配色与筛选等公共逻辑。
 * - 特殊轴 key 全部通过 EvaluationProtocolKeys 映射函数获取。
 * - 方法子类继承本组件；异常检测专用矩阵在后续阶段实现。
 */
Rectangle {
    id: control
    color: QuiColor.Primary
    property ModelEvaluationViewModel evaluation: null
    property string title: qsTr("混淆矩阵")
    property var classColumnLabels: []
    property var classRowLabels: []
    property int matrixRowCount: 0
    readonly property real leftAxisWidth: 48
    readonly property real rightLabelsWidth: 104
    readonly property real bottomLabelsHeight: 72

    function isTotalKey(key) {
        return String(key) === EvaluationProtocolKeys.matrixAxisTotal
    }

    function classColorForLabel(classId, key, label, predicted) {
        const text = String(label || "")
        if (predicted && (text === "正常" || text === "Good"))
            return "#00b85a"
        if (predicted && (text === "异常" || text === "Anomaly"))
            return "#d71920"

        const palette = ["#ef5350", "#42a5f5", "#66bb6a", "#ffa726",
                         "#ab47bc", "#26c6da", "#8d6e63", "#78909c"]
        const value = Number(classId)
        if (!isFinite(value) || value < 0)
            return QuiColor.FontDark
        return palette[Math.floor(value) % palette.length]
    }

    function isUnmatchedPredictionKey(key) {
        return String(key) === EvaluationProtocolKeys.matrixAxisUnmatchedPrediction
    }

    function isUnmatchedGroundTruthKey(key) {
        return String(key) === EvaluationProtocolKeys.matrixAxisUnmatchedGroundTruth
    }

    function isSpecialColumn(key) {
        const value = String(key)
        return value === EvaluationProtocolKeys.matrixAxisFalsePositive || control.isUnmatchedPredictionKey(value) || control.isTotalKey(value)
    }

    function isSpecialRow(key) {
        const value = String(key)
        return value === EvaluationProtocolKeys.matrixAxisFalseNegative || control.isUnmatchedGroundTruthKey(value) || control.isTotalKey(value)
    }

    /**
     * @brief 判断单元格是否属于错误区域：FP/误检、FN/漏检 及其合计与不可用格。
     * @param kind 单元格类型（EvaluationConfusionModel.CellKindValueRole）。
     */
    function isErrorKind(kind) {
        if (kind === EvaluationConfusionModel.CellKindFalsePositive
                || kind === EvaluationConfusionModel.CellKindFalseNegative
                || kind === EvaluationConfusionModel.CellKindNotApplicable
                || kind === EvaluationConfusionModel.CellKindFalsePositiveTotal
                || kind === EvaluationConfusionModel.CellKindFalseNegativeTotal)
            return true
        return false
    }

    /**
     * @brief 判断单元格是否为类别合计格（TOTAL 行列上对类别/全部实例的聚合）。
     */
    function isTotalKind(kind) {
        if (kind === EvaluationConfusionModel.CellKindAll
                || kind === EvaluationConfusionModel.CellKindPredTotal
                || kind === EvaluationConfusionModel.CellKindGtTotal)
            return true
        return false
    }

    /**
     * @brief 计算单元格底色：FP/FN/漏检/误检 等错误轴上的格子统一用背景色，
     *        类别合计与正确匹配用高亮色，其余（类别错配）用主色。
     */
    function cellBaseColor(kind, diagonal, errorAxis, totalAxis) {
        if (totalAxis)
            return QuiColor.Highlight
        if (errorAxis)
            return QuiColor.Background
        if (control.isTotalKind(kind))
            return QuiColor.Highlight
        if (kind === EvaluationConfusionModel.CellKindMatch && diagonal)
            return QuiColor.Highlight
        if (control.isErrorKind(kind))
            return QuiColor.Background
        return QuiColor.Primary
    }

    function cellFillColor(kind, diagonal, errorAxis, totalAxis, selected) {
        if (selected)
            return Qt.darker(control.cellBaseColor(kind, diagonal, errorAxis, totalAxis))
        return control.cellBaseColor(kind, diagonal, errorAxis, totalAxis)
    }

    function cellOpacity(kind, count, selected, errorAxis, totalAxis) {
        if (selected || errorAxis || totalAxis || count > 0
                || kind === EvaluationConfusionModel.CellKindAll)
            return 1.0
        return 0.72
    }

    function cellBorderColor(kind, errorAxis, totalAxis, selected) {
        if (selected)
            return QuiColor.Highlight
        if (totalAxis || (control.isTotalKind(kind) && !errorAxis))
            return QuiColor.FontDark
        return QuiColor.Border
    }

    function cellBorderWidth(selected) {
        if (selected)
            return 2
        return 1
    }

    function isErrorAxisRow(key) {
        const value = String(key)
        return value === EvaluationProtocolKeys.matrixAxisFalseNegative || control.isUnmatchedGroundTruthKey(value)
    }

    function isErrorAxisColumn(key) {
        const value = String(key)
        return value === EvaluationProtocolKeys.matrixAxisFalsePositive || control.isUnmatchedPredictionKey(value)
    }

    function isCellSelected(rowKey, columnKey) {
        if (!control.evaluation)
            return false
        const row = control.evaluation.filteredInstances.matrixRow
        const column = control.evaluation.filteredInstances.matrixColumn
        /* 空矩阵筛选表示全部实例，默认选中右下角的 TOTAL/TOTAL 单元格。 */
        if (!row && !column)
            return rowKey === EvaluationProtocolKeys.matrixAxisTotal && columnKey === EvaluationProtocolKeys.matrixAxisTotal
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
                let specialColumn = control.isSpecialColumn(key)
                columnSource.push({
                    title: specialColumn
                            ? confusionTable.customItem(com_matrix_header, {
                                label: label,
                                isTotal: control.isTotalKey(key)
                            }) : "",
                    dataIndex: key,
                    width: control.isUnmatchedPredictionKey(key) ? 44 : 32,
                    minimumWidth: control.isUnmatchedPredictionKey(key) ? 44 : 32,
                    stretch: false,
                    frozen: specialColumn
                })
                if (!specialColumn)
                    classColumnLabels.push({
                        column: c,
                        label: String(label),
                        classId: control.matrixData(0, c, EvaluationConfusionModel.ColumnClassIdRole, -1),
                        key: String(key)
                    })
            }
            for (let r = 0; r < rowCount; ++r) {
                let rowKey = control.matrixData(r, 0, EvaluationConfusionModel.RowKeyRole, "")
                let rowLabel = control.matrixData(r, 0, EvaluationConfusionModel.RowLabelRole, "")
                if (!control.isSpecialRow(rowKey))
                    classRowLabels.push({
                        row: r,
                        label: String(rowLabel),
                        classId: control.matrixData(r, 0, EvaluationConfusionModel.RowClassIdRole, -1),
                        key: String(rowKey)
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
        /*
         * 无结果是重新评估过程中的短暂数据状态，不是列结构变化。
         * 保留列与冻结列，避免在 TableView 回收旧 delegate 时拆除子视图。
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
            readonly property string rowKey: String(options && options.rowKey !== undefined ? options.rowKey : "")
            readonly property string columnKey: String(options && options.columnKey !== undefined ? options.columnKey : "")
            readonly property int cellKind: Number(options && options.cellKind !== undefined
                                                   ? options.cellKind
                                                   : EvaluationConfusionModel.CellKindNotApplicable)
            readonly property bool selectable: !!(options && options.selectable)
            readonly property int count: Number(options && options.count !== undefined ? options.count : 0)
            property bool selected: control.isCellSelected(rowKey, columnKey)
            property bool errorAxisCell: control.isErrorAxisRow(rowKey)
                                      || control.isErrorAxisColumn(columnKey)
            property bool totalAxisCell: control.isTotalKey(rowKey)
                                      || control.isTotalKey(columnKey)
            objectName: "confusionCell_" + rowKey + "_" + columnKey
            color: control.cellFillColor(cellKind, !!options.isDiagonal,
                                         errorAxisCell, totalAxisCell, selected)
            opacity: control.cellOpacity(cellKind, count, selected,
                                         errorAxisCell, totalAxisCell)
            border.color: control.cellBorderColor(cellKind, errorAxisCell,
                                                   totalAxisCell, selected)
            border.width: control.cellBorderWidth(selected)

            Column {
                anchors.centerIn: parent
                QuiText {
                    anchors.horizontalCenter: parent.horizontalCenter
                    // Dynamic table delegates can be created before their options are injected.
                        text: String(parent.parent.count)
                    color: QuiColor.FontPrimary
                }
            }
            MouseArea {
                id: cellMouse
                anchors.fill: parent
                enabled: selectable
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
        anchors.margins: 5

        // 顶栏 Header 容器（包含标题）
        RowLayout {
            id: headerHost
            Layout.fillWidth: true
            Layout.preferredHeight: 32

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
                /* 同一评估对象始终暴露同一个矩阵模型，空矩阵也不切换 model。 */
                externalModel: control.evaluation ? control.evaluation.confusionMatrix : null
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
                        property bool classRow: !control.isSpecialRow(rowKey)
                        implicitWidth: control.leftAxisWidth
                        implicitHeight: confusionTable.currentRowHeight(sourceRow)
                        color: confusionTable.headerColor
                        border.color: confusionTable.borderColor
                        border.width: 1

                        QuiTextIcon {
                            anchors.centerIn: parent
                            visible: parent.rowKey === EvaluationProtocolKeys.matrixAxisTotal
                            iconSource: QuiFontIcon.Picture
                            iconColor: confusionTable.headerTextColor
                        }
                        QuiText {
                            id: rowLabel
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            visible: parent.rowKey !== EvaluationProtocolKeys.matrixAxisTotal
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
                        color: QuiColor.Transparent

                        Rectangle {
                            id: predictedClassColor
                            x: 8
                            width: 10
                            height: 10
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 2
                            color: control.classColorForLabel(modelData.classId, modelData.key,
                                                              modelData.label, true)
                        }

                        QuiText {
                            anchors.left: predictedClassColor.right
                            anchors.leftMargin: 4
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            height: parent.height
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

                        Item {
                            id: groundTruthClassLabel
                            width: Math.max(0, bottomClassLabels.height - 12)
                            height: parent.width
                            anchors.centerIn: parent
                            transformOrigin: Item.Center
                            rotation: 90

                            Rectangle {
                                id: groundTruthClassColor
                                x: 0
                                width: 10
                                height: 10
                                anchors.verticalCenter: parent.verticalCenter
                                radius: 2
                                color: control.classColorForLabel(modelData.classId, modelData.key,
                                                                  modelData.label, false)
                            }

                            QuiText {
                                x: groundTruthClassColor.width + 4
                                y: 0
                                width: Math.max(0, parent.width - x)
                                height: parent.height
                                text: modelData.label
                                color: confusionTable.headerTextColor
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
