import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

Rectangle {
    id: control
    width: 200
    height: 200
    color: QuiColor.Primary
    border.color: selected ? QuiColor.Highlight : QuiColor.Background
    border.width: selected ? 2 : 1

    property var modelId: -1
    property string modelUuid: ""
    property string modelName: ""
    property string networkStructure: ""
    property string trainingResult: ""
    property string testResult: ""
    property bool selected: false
    property bool showTaskActions: false
    property bool taskActionsEnabled: false

    signal startClicked()
    signal stopClicked()

    ColumnLayout {
        z: 1
        anchors.fill: parent
        anchors.margins: 10
        spacing: 0

        QuiText {
            Layout.fillWidth: true
            text: control.modelName
            font: QuiFont.BodyStrong
            elide: Text.ElideRight
        }

        InfoRow {
            title: "网络结构"
            value: control.networkStructure
        }
        InfoRow {
            title: "训练结果"
            value: control.trainingResult
        }
        InfoRow {
            title: "测试结果"
            value: control.testResult
        }

        Item {
            Layout.fillHeight: true
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: visible ? 34 : 0
            spacing: 8
            visible: control.selected && control.showTaskActions
            z: 2

            QuiTextIconButton {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 30
                display: Button.IconOnly
                text: "开始"
                iconSource: QuiFontIcon.Play
                enabled: control.taskActionsEnabled
                onClicked: control.startClicked()
            }

            QuiTextIconButton {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 30
                display: Button.IconOnly
                text: "停止"
                iconSource: QuiFontIcon.Stop
                enabled: control.taskActionsEnabled
                onClicked: control.stopClicked()
            }
        }
    }

    component InfoRow: RowLayout {
        property string title: ""
        property string value: ""
        spacing: 5

        QuiText {
            Layout.preferredWidth: 80
            text: title + ":"
            color: QuiColor.FontDark
            elide: Text.ElideRight
        }
        QuiText {
            Layout.fillWidth: true
            text: value
            elide: Text.ElideRight
        }
    }
}
