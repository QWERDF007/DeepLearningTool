import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

Rectangle {
    id: sidebar
    color: QuiColor.Primary
    border.color: QuiColor.Border

    property string currentTool: "select"
    property bool segmentationMode: false
    property bool smartAnnotationAvailable: false
    property bool dataManagerAvailable: false
    property bool fewShotLearningAvailable: false

    signal toolSelected(string mode)
    signal openFewShotLearning()

    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 8
        spacing: 6

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            normalColor: sidebar.currentTool === "select" ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.TouchPointer
            text: "选中"
            onClicked: sidebar.toolSelected("select")
        }

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            normalColor: sidebar.currentTool === "rect" ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.RectangularClipping
            text: "绘制矩形"
            onClicked: sidebar.toolSelected("rect")
        }

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            enabled: sidebar.segmentationMode
            normalColor: sidebar.currentTool === "polygon" ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.FreeFormClipping
            text: "绘制多边形"
            onClicked: sidebar.toolSelected("polygon")
        }

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            enabled: sidebar.smartAnnotationAvailable
            normalColor: sidebar.currentTool === "smart" ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.Touchscreen
            text: "智能标注"
            onClicked: sidebar.toolSelected("smart")
        }

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            enabled: sidebar.fewShotLearningAvailable
            iconSource: QuiFontIcon.Robot
            text: "小样本学习"
            onClicked: sidebar.openFewShotLearning()
        }
    }
}
