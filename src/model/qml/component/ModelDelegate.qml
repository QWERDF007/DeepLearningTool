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
    property string modelName: ""
    property string networkStructure: ""
    property string trainingResult: ""
    property string testResult: ""
    property bool selected: false

    ColumnLayout {
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
