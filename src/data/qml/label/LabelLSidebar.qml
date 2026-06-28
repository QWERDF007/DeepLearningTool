import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.core
import dltool.ui
import dltool.feature
import quickui

Rectangle {
    id: sidebar
    color: QuiColor.Primary
    border.color: QuiColor.Border

    property string currentTool: "select"
    property bool segmentationMode: false

    property FeatureManager featureManager: null
    property DataManager dataManager: null

    signal toolSelected(string mode)

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
            enabled: sidebar.featureManager !== null && sidebar.featureManager.smartAnnotation && sidebar.featureManager.smartAnnotation.enabled
                     && dataManager && (dataManager.method === DeepLearningMethod.Detection || dataManager.method === DeepLearningMethod.Segmentation)
            normalColor: sidebar.currentTool === "smart" ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.Touchscreen
            text: "智能标注"
            onClicked: sidebar.toolSelected("smart")
        }

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            enabled: sidebar.featureManager !== null && sidebar.featureManager.fewShotLearning && sidebar.featureManager.fewShotLearning.enabled
            iconSource: QuiFontIcon.Robot
            text: "小样本学习"
            onClicked: fewShotLearningDialog.openForStart()
        }
    }

    FewShotLearningDialog {
        id: fewShotLearningDialog
        dataManager: sidebar.dataManager
        featureManager: sidebar.featureManager
    }
}
