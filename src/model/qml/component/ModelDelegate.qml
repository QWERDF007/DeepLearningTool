import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    id: control
    width: 200
    height: 200
    color: DltColor.Primary
    border.color: selected ? DltColor.Highlight : DltColor.Background
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

        DltText {
            Layout.fillWidth: true
            text: control.modelName
            font: DltFont.BodyStrong
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

        DltText {
            Layout.preferredWidth: 80
            text: title + ":"
            color: DltColor.FontDark
            elide: Text.ElideRight
        }
        DltText {
            Layout.fillWidth: true
            text: value
            elide: Text.ElideRight
        }
    }
}
