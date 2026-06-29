import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.core
import dltool.data
import dltool.ui
import dltool.feature
import quickui

Rectangle {
    id: sidebar
    color: QuiColor.Primary
    border.color: QuiColor.Border

    property int currentTool: LabelCanvasEnums.SelectTool
    property bool segmentationMode: false

    property FeatureManager featureManager: null
    property DataManager dataManager: null

    signal toolSelected(int mode)

    readonly property bool smartAnnotationEnabled: sidebar.featureManager !== null
                                                   && sidebar.featureManager.smartAnnotation !== null
                                                   && sidebar.featureManager.smartAnnotation.enabled
                                                   && sidebar.dataManager !== null
                                                   && (sidebar.dataManager.method === DeepLearningMethod.Detection
                                                       || sidebar.dataManager.method === DeepLearningMethod.Segmentation)

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
            normalColor: sidebar.currentTool === LabelCanvasEnums.SelectTool ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.TouchPointer
            text: "选中"
            onClicked: sidebar.toolSelected(LabelCanvasEnums.SelectTool)
        }

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            normalColor: sidebar.currentTool === LabelCanvasEnums.RectangleTool ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.RectangularClipping
            text: "绘制矩形"
            onClicked: sidebar.toolSelected(LabelCanvasEnums.RectangleTool)
        }

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            enabled: sidebar.segmentationMode
            normalColor: sidebar.currentTool === LabelCanvasEnums.PolygonTool ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.FreeFormClipping
            text: "绘制多边形"
            onClicked: sidebar.toolSelected(LabelCanvasEnums.PolygonTool)
        }

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            enabled: sidebar.smartAnnotationEnabled
            normalColor: sidebar.currentTool === LabelCanvasEnums.SmartTool ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.Touchscreen
            text: "智能标注"
            onClicked: sidebar.toolSelected(LabelCanvasEnums.SmartTool)
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
