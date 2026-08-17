import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

/*
 * 图像指标面板 Base：
 * - 提供百分比/饼图数据构造等公共函数与标题外壳。
 * - 方法子类通过 default property 注入指标内容。
 */
Rectangle {
    id: control
    clip: true
    color: QuiColor.Primary
    property ModelEvaluationViewModel evaluation: null
    property string title: qsTr("图像统计")
    property string emptyText: qsTr("当前方法没有图像指标")

    default property alias content: contentHost.data
    property alias headerContent: headerAction.data

    function percentage(value, decimals) {
        var number = Number(value)
        if (!isFinite(number))
            number = 0
        var text = (Math.max(0, Math.min(1, number)) * 100).toFixed(decimals)
        if (decimals > 0)
            text = text.replace(/\.?(0+)$/, "")
        return text + "%"
    }

    function pieData(value) {
        var number = Number(value)
        if (!isFinite(number))
            number = 0
        number = Math.max(0, Math.min(1, number)) * 100
        return ({
            labels: [qsTr("正确"), qsTr("错误")],
            datasets: [{
                data: [number, 100 - number],
                backgroundColor: ["#00b85a", "#d71920"],
                borderColor: ["#e8e8e8", "#e8e8e8"],
                borderWidth: 1
            }]
        })
    }

    function pieOptions() {
        return ({
            maintainAspectRatio: false,
            responsive: true,
            legend: { display: false },
            tooltips: { enabled: false },
            animation: { duration: 0 }
        })
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        // 顶栏 Header 容器（包含标题与右上角动作插槽）
        RowLayout {
            id: headerHost
            Layout.fillWidth: true

            QuiText {
                text: control.title
                font: QuiFont.Subtitle
            }

            Item { Layout.fillWidth: true }

            Item {
                id: headerAction
                visible: children.length > 0
                Layout.preferredWidth: visible ? 100 : 0
                Layout.preferredHeight: visible ? 28 : 0
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            }
        }

        Item {
            id: contentHost
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
