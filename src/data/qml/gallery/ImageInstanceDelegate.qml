import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import dltool.core
import dltool.ui
import dltool.settings
import dltool.data
import quickui

Rectangle {
    id: imageInstanceDelegate
    property alias image: image
    property var image_id: -1
    property var imageLevelLabelClassId: model.image_label_class_id !== undefined ? Number(model.image_label_class_id) : -1
    property bool selected: false
    property bool hasLabels: model.hasLabels || false
    property DataManager dataManager
    property real imageBrightness: 0.0
    property real imageContrast: 0.0
    property int labelClassesRevision: 0
    readonly property bool imageLevelBadgeMode: dataManager && dataManager.method === DeepLearningMethod.AnomalyDetection
    readonly property var imageLevelLabelData: findImageLevelLabelData()
    
    property string labelSummary: {
        if (imageLevelBadgeMode) {
            return imageLevelLabelData ? String(imageLevelLabelData.name || "") : ""
        }
        if (!hasLabels || image_id < 0 || !dataManager || !dataManager.imageLabelsList) {
            return ""
        }
        return dataManager.imageLabelsList.getLabelSummaryForImage(image_id)
    }

    property string labelColor: {
        if (imageLevelBadgeMode) {
            return imageLevelLabelData ? String(imageLevelLabelData.color || "") : ""
        }
        if (!hasLabels || image_id < 0 || !dataManager || !dataManager.imageLabelsList) {
            return ""
        }
        return dataManager.imageLabelsList.getLabelColorForImage(image_id)
    }

    width: 320
    height: 240
    color: QuiColor.Transparent
    border.color: selected ? QuiColor.Highlight : QuiColor.Border
    border.width: 3


    Image {
        id: image
        visible: false
        anchors.fill: parent
        anchors.margins: 5
        fillMode: Image.PreserveAspectFit
        sourceSize.width: image.width
        sourceSize.height: image.height
        asynchronous: true
        cache: false
    }
    
    BusyIndicator {
        anchors.centerIn: parent
        running: image.status === Image.Loading
    }

    MultiEffect {
        source: image
        anchors.fill: image
        visible: image.status === Image.Ready && image.source.toString().length > 0
        brightness: imageInstanceDelegate.imageBrightness
        contrast: imageInstanceDelegate.imageContrast
    }

    function refreshSettings() {
        imageBrightness = GlobalSettings.valueForField(SettingsAccessor.Ui, UiField.Brightness, 0.0)
        imageContrast = GlobalSettings.valueForField(SettingsAccessor.Ui, UiField.Contrast, 0.0)
    }

    function findImageLevelLabelData() {
        let revision = labelClassesRevision
        if (revision < 0 || !imageLevelBadgeMode || !dataManager || imageLevelLabelClassId < 0) {
            return null
        }

        let labelClasses = dataManager.labelClasses
        if (!labelClasses) {
            return null
        }

        let classId = Number(imageLevelLabelClassId)
        for (let row = 0; row < labelClasses.rowCount(); ++row) {
            let modelIndex = labelClasses.index(row, 0)
            let rowClassId = Number(labelClasses.data(modelIndex, LabelClassesModel.LabelClassIdRole))
            if (rowClassId !== classId) {
                continue
            }

            return {
                "name": String(labelClasses.data(modelIndex, LabelClassesModel.NameRole) || ""),
                "color": String(labelClasses.data(modelIndex, LabelClassesModel.ColorRole) || "")
            }
        }
        return null
    }

    function badgeVisible() {
        if (imageLevelBadgeMode) {
            return labelSummary !== "" && labelColor !== ""
        }
        return hasLabels && labelColor !== ""
    }

    Component.onCompleted: refreshSettings()

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            imageInstanceDelegate.refreshSettings()
        }
    }

    Connections {
        target: imageInstanceDelegate.dataManager ? imageInstanceDelegate.dataManager.labelClasses : null
        function onRowsInserted(parent, first, last) { imageInstanceDelegate.labelClassesRevision += 1 }
        function onRowsRemoved(parent, first, last) { imageInstanceDelegate.labelClassesRevision += 1 }
        function onDataChanged(topLeft, bottomRight, roles) { imageInstanceDelegate.labelClassesRevision += 1 }
        function onModelReset() { imageInstanceDelegate.labelClassesRevision += 1 }
    }
    
    // 标注指示器 - 显示在右上角
    Rectangle {
        id: annotationIndicator
        visible: imageInstanceDelegate.badgeVisible()
        width: 12
        height: 12
        radius: 2
        color: labelColor
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 4
        
        // 鼠标悬浮区域
        MouseArea {
            id: hoverArea
            anchors.fill: parent
            hoverEnabled: true
        }
        
        QuiToolTip {
            visible: hoverArea.containsMouse && labelSummary !== ""
            text: labelSummary
            delay: 500
        }
    }
}
