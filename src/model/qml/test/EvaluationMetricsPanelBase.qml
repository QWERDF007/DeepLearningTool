import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.model
import dltool.ui
import quickui

/*
 * 实例指标面板 Base：
 * - 提供标题、header 注入点与内容注入点（default property）。
 * - 方法子类只注入自己的指标内容与弹窗，不重复外壳布局。
 */
Rectangle {
    id: control
    color: QuiColor.Primary
    property ModelEvaluationViewModel evaluation: null
    property string title: qsTr("实例统计")
    property string emptyText: qsTr("当前方法没有实例指标")

    default property alias content: contentHost.data
    property alias headerContent: headerAction.data

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5

        // 顶栏 Header 容器（包含标题与右上角动作插槽）
        RowLayout {
            id: headerHost
            Layout.fillWidth: true
            Layout.preferredHeight: 32

            QuiText {
                text: control.title
                font: QuiFont.Subtitle
            }

            Item { Layout.fillWidth: true }

            Item {
                id: headerAction
                visible: children.length > 0
                Layout.preferredWidth: visible ? 100 : 0
                Layout.preferredHeight: visible ? 32 : 0
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
