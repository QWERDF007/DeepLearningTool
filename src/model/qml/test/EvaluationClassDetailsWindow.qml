import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import dltool.model
import dltool.ui
import quickui

/*
 * 类别指标详情独立窗口（Window）
 * 风格与 TaskCenterWindow 保持一致：
 * - 独立非模态窗口，支持最大化、关闭、Esc退出
 * - 基于 QuiTableView 多列报表，按类别默认顺序排列
 * - 精确率、召回率列嵌入红绿比例迷你饼图
 * - 统一字体与无边框风格
 */
Window {
    id: window

    property var evaluation: null

    visible: false
    title: qsTr("类别指标详情")
    width: 960
    height: 560
    minimumWidth: 800
    minimumHeight: 400
    color: QuiColor.Primary
    modality: Qt.NonModal
    flags: Qt.Window | Qt.CustomizeWindowHint | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMaximizeButtonHint | Qt.WindowCloseButtonHint

    Shortcut {
        sequence: "Esc"
        onActivated: window.close()
    }

    function percentage(value, decimals) {
        if (value === undefined || value === null || isNaN(value)) {
            return "—"
        }
        return (value * 100).toFixed(decimals !== undefined ? decimals : 1) + "%"
    }

    function rebuildClassTable() {
        if (!classTableView)
            return
        let rows = []
        if (window.evaluation && window.evaluation.perClassMetrics) {
            let model = window.evaluation.perClassMetrics
            let count = model ? model.rowCount() : 0
            for (let i = 0; i < count; ++i) {
                let idx = model.index(i, 0)
                let classId = model.data(idx, EvaluationMetricModel.ClassIdRole)
                let label = model.data(idx, EvaluationMetricModel.LabelRole)
                let className = model.data(idx, EvaluationMetricModel.ClassNameRole)
                let classColor = model.data(idx, EvaluationMetricModel.ClassColorRole)
                let precision = model.data(idx, EvaluationMetricModel.PrecisionRole)
                let recall = model.data(idx, EvaluationMetricModel.RecallRole)
                let f1 = model.data(idx, EvaluationMetricModel.F1Role)
                let ap = model.data(idx, EvaluationMetricModel.ApRole)
                let apText = model.data(idx, EvaluationMetricModel.ApTextRole)
                let tp = model.data(idx, EvaluationMetricModel.TpRole)
                let fp = model.data(idx, EvaluationMetricModel.FpRole)
                let fn = model.data(idx, EvaluationMetricModel.FnRole)
                let fpPredictedText = model.data(idx, EvaluationMetricModel.FpPredictedTextRole)
                let fnLabeledText = model.data(idx, EvaluationMetricModel.FnLabeledTextRole)

                rows.push({
                    classId: classId,
                    label: label,
                    className: className,
                    classColor: classColor,
                    precision: precision,
                    recall: recall,
                    f1: f1,
                    ap: ap,
                    apText: apText,
                    tp: tp,
                    fp: fp,
                    fn: fn,
                    fpPredictedText: fpPredictedText,
                    fnLabeledText: fnLabeledText,
                    class_cell: classTableView.customItem(com_class_cell),
                    fp_cell: classTableView.customItem(com_fp_cell),
                    precision_cell: classTableView.customItem(com_precision_cell),
                    fn_cell: classTableView.customItem(com_fn_cell),
                    recall_cell: classTableView.customItem(com_recall_cell),
                    f1_cell: classTableView.customItem(com_f1_cell),
                    ap_cell: classTableView.customItem(com_ap_cell)
                })
            }
        }
        classTableView.dataSource = rows
    }

    Connections {
        target: window.evaluation ? window.evaluation.perClassMetrics : null
        function onModelReset() { window.rebuildClassTable() }
        function onRowsInserted() { window.rebuildClassTable() }
        function onRowsRemoved() { window.rebuildClassTable() }
        function onLayoutChanged() { window.rebuildClassTable() }
        function onCountChanged() { window.rebuildClassTable() }
    }

    onVisibleChanged: {
        if (visible) {
            rebuildClassTable()
        }
    }

    Item {
        anchors.fill: parent
        anchors.margins: 12

        QuiTableView {
            id: classTableView
            anchors.fill: parent
            rowHeight: 32
            headerHeight: 32
            headerColor: QuiColor.Background
            borderColor: QuiColor.Border
            showGridLines: true
            fitColumnsToWidth: true
            minimumColumnWidth: 70
            rowColor: Qt.lighter(QuiColor.Primary, 1.3)
            rowSelectionEnabled: false
            hoverEnabled: false
            zebraEnabled: false
            columnSource: [
                { title: qsTr("类别"), dataIndex: "class_cell", width: 140, minimumWidth: 100, resizable: true },
                { title: qsTr("FP / 预测数"), dataIndex: "fp_cell", width: 130, minimumWidth: 90, resizable: true },
                { title: qsTr("精确率"), dataIndex: "precision_cell", width: 120, minimumWidth: 90, resizable: true },
                { title: qsTr("FN / 标注数"), dataIndex: "fn_cell", width: 130, minimumWidth: 90, resizable: true },
                { title: qsTr("召回率"), dataIndex: "recall_cell", width: 120, minimumWidth: 90, resizable: true },
                { title: qsTr("F1-Score"), dataIndex: "f1_cell", width: 110, minimumWidth: 80, resizable: true },
                { title: qsTr("AP"), dataIndex: "ap_cell", width: 100, minimumWidth: 70, resizable: true }
            ]
            dataSource: []
        }
    }

    Component {
        id: com_class_cell
        Item {
            anchors.fill: parent
            RowLayout {
                anchors.centerIn: parent
                spacing: 6

                Rectangle {
                    width: 12
                    height: 12
                    radius: 2
                    color: rowModel ? rowModel.classColor : QuiColor.FontDark
                }

                QuiText {
                    text: rowModel ? (rowModel.label || rowModel.className || rowModel.key) : ""
                    elide: Text.ElideRight
                    font: QuiFont.Body
                }
            }
        }
    }

    Component {
        id: com_fp_cell
        Item {
            anchors.fill: parent
            QuiText {
                anchors.centerIn: parent
                text: rowModel ? rowModel.fpPredictedText : ""
                font: QuiFont.Body
            }
        }
    }

    Component {
        id: com_precision_cell
        Item {
            anchors.fill: parent
            RowLayout {
                anchors.centerIn: parent
                spacing: 6

                Canvas {
                    width: 14
                    height: 14
                    property real val: rowModel ? rowModel.precision : NaN
                    onValChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.clearRect(0, 0, width, height);
                        var cx = width / 2;
                        var cy = height / 2;
                        var r = Math.min(cx, cy) - 0.5;

                        if (isNaN(val) || val === undefined || val === null) {
                            ctx.beginPath();
                            ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                            ctx.fillStyle = "#6e6e6e";
                            ctx.fill();
                            return;
                        }

                        var v = Math.max(0.0, Math.min(1.0, val));
                        if (v >= 0.9999) {
                            ctx.beginPath();
                            ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                            ctx.fillStyle = "#00b85a";
                            ctx.fill();
                            return;
                        }
                        if (v <= 0.0001) {
                            ctx.beginPath();
                            ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                            ctx.fillStyle = "#d71920";
                            ctx.fill();
                            return;
                        }

                        // Base red circle
                        ctx.beginPath();
                        ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                        ctx.fillStyle = "#d71920";
                        ctx.fill();

                        // Green slice
                        ctx.beginPath();
                        ctx.moveTo(cx, cy);
                        ctx.arc(cx, cy, r, -Math.PI / 2, -Math.PI / 2 + v * 2 * Math.PI, false);
                        ctx.closePath();
                        ctx.fillStyle = "#00b85a";
                        ctx.fill();
                    }
                }

                QuiText {
                    text: rowModel ? window.percentage(rowModel.precision, 1) : "—"
                    font: QuiFont.Body
                }
            }
        }
    }

    Component {
        id: com_fn_cell
        Item {
            anchors.fill: parent
            QuiText {
                anchors.centerIn: parent
                text: rowModel ? rowModel.fnLabeledText : ""
                font: QuiFont.Body
            }
        }
    }

    Component {
        id: com_recall_cell
        Item {
            anchors.fill: parent
            RowLayout {
                anchors.centerIn: parent
                spacing: 6

                Canvas {
                    width: 14
                    height: 14
                    property real val: rowModel ? rowModel.recall : NaN
                    onValChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.clearRect(0, 0, width, height);
                        var cx = width / 2;
                        var cy = height / 2;
                        var r = Math.min(cx, cy) - 0.5;

                        if (isNaN(val) || val === undefined || val === null) {
                            ctx.beginPath();
                            ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                            ctx.fillStyle = "#6e6e6e";
                            ctx.fill();
                            return;
                        }

                        var v = Math.max(0.0, Math.min(1.0, val));
                        if (v >= 0.9999) {
                            ctx.beginPath();
                            ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                            ctx.fillStyle = "#00b85a";
                            ctx.fill();
                            return;
                        }
                        if (v <= 0.0001) {
                            ctx.beginPath();
                            ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                            ctx.fillStyle = "#d71920";
                            ctx.fill();
                            return;
                        }

                        // Base red circle
                        ctx.beginPath();
                        ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                        ctx.fillStyle = "#d71920";
                        ctx.fill();

                        // Green slice
                        ctx.beginPath();
                        ctx.moveTo(cx, cy);
                        ctx.arc(cx, cy, r, -Math.PI / 2, -Math.PI / 2 + v * 2 * Math.PI, false);
                        ctx.closePath();
                        ctx.fillStyle = "#00b85a";
                        ctx.fill();
                    }
                }

                QuiText {
                    text: rowModel ? window.percentage(rowModel.recall, 1) : "—"
                    font: QuiFont.Body
                }
            }
        }
    }

    Component {
        id: com_f1_cell
        Item {
            anchors.fill: parent
            QuiText {
                anchors.centerIn: parent
                text: rowModel ? window.percentage(rowModel.f1, 2) : "—"
                font: QuiFont.Body
            }
        }
    }

    Component {
        id: com_ap_cell
        Item {
            anchors.fill: parent
            QuiText {
                anchors.centerIn: parent
                text: rowModel ? (rowModel.apText || (rowModel.ap > 0 ? window.percentage(rowModel.ap, 1) : "—")) : "—"
                font: QuiFont.Body
            }
        }
    }
}
