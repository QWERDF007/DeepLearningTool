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

    property FeatureManager featureManager: null
    property DataManager dataManager: null
    property int currentTool: LabelCanvasEnums.SelectTool

    readonly property int method: dataManager ? dataManager.method : -1
    readonly property bool classificationMode: method === DeepLearningMethod.Classification
    readonly property bool detectionMode: method === DeepLearningMethod.Detection
    readonly property bool segmentationMode: method === DeepLearningMethod.Segmentation
    readonly property bool rectangleToolEnabled: detectionMode || segmentationMode
    readonly property bool polygonToolEnabled: segmentationMode

    signal toolSelected(int mode)

    readonly property bool smartAnnotationEnabled: sidebar.featureManager !== null
                                                   && sidebar.featureManager.smartAnnotation !== null
                                                   && sidebar.featureManager.smartAnnotation.enabled
                                                   && (sidebar.detectionMode || sidebar.segmentationMode)
    readonly property bool fewShotLearningEnabled: sidebar.featureManager !== null
                                                   && sidebar.featureManager.fewShotLearning !== null
                                                   && sidebar.featureManager.fewShotLearning.enabled

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
            text: "选中 (F1)"
            onClicked: sidebar.toolSelected(LabelCanvasEnums.SelectTool)
        }

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            normalColor: sidebar.currentTool === LabelCanvasEnums.RectangleTool ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.RectangularClipping
            text: "绘制矩形 (F2)"
            enabled: sidebar.rectangleToolEnabled
            onClicked: sidebar.toolSelected(LabelCanvasEnums.RectangleTool)
        }

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            enabled: sidebar.polygonToolEnabled
            normalColor: sidebar.currentTool === LabelCanvasEnums.PolygonTool ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.FreeFormClipping
            text: "绘制多边形 (F3)"
            onClicked: sidebar.toolSelected(LabelCanvasEnums.PolygonTool)
        }

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            enabled: sidebar.smartAnnotationEnabled
            normalColor: sidebar.currentTool === LabelCanvasEnums.SmartTool ? QuiColor.Highlight : QuiColor.Button
            iconSource: QuiFontIcon.Touchscreen
            text: "智能标注 (F4)"
            onClicked: sidebar.toolSelected(LabelCanvasEnums.SmartTool)
        }

        QuiTextIconButton {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            enabled: sidebar.fewShotLearningEnabled
            iconSource: QuiFontIcon.Robot
            text: "小样本学习 (F5)"
            onClicked: fewShotLearningDialog.openForStart()
        }
    }

    Shortcut {
        enabled: sidebar.visible && sidebar.fewShotLearningEnabled
        sequence: "F5"
        onActivated: fewShotLearningDialog.openForStart()
    }

    FewShotLearningDialog {
        id: fewShotLearningDialog
        dataManager: sidebar.dataManager
        featureManager: sidebar.featureManager
    }
}
