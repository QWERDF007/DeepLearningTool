import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import dltool.ui
import dltool.project
import dltool.data

Rectangle {
    id: imageInstanceDelegate
    property alias image: image
    property int image_id: -1
    property bool selected: false
    property bool hasLabels: model.hasLabels || false
    property DataManager dataManager
    
    // 新增：通过 image_id 动态获取标注信息
    property string labelSummary: {
        if (!hasLabels || image_id < 0 || !dataManager || !dataManager.imageLabelsList) return "";
        return dataManager.imageLabelsList.getLabelSummaryForImage(image_id);
    }
    
    property string labelColor: {
        if (!hasLabels || image_id < 0 || !dataManager || !dataManager.imageLabelsList) return "";
        return dataManager.imageLabelsList.getLabelColorForImage(image_id);
    }

    width: 320
    height: 240
    color: "transparent"
    border.color: selected ? DltColor.Highlight : DltColor.Border
    border.width: 2


    Image {
        id: image
        visible: false
        anchors.fill: parent
        anchors.margins: 2
        fillMode: Image.PreserveAspectFit
        sourceSize.width: image.width
        sourceSize.height: image.height
        asynchronous: true
    }
    
    BusyIndicator {
        anchors.centerIn: parent
        running: image.status === Image.Loading
    }

    MultiEffect {
        source: image
        anchors.fill: image
        brightness: Settings.imageBrightness
        contrast: Settings.imageContrast
    }
    
    // 标注指示器 - 显示在右上角
    Rectangle {
        id: annotationIndicator
        visible: hasLabels && labelColor !== ""
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
        
        DltToolTip {
            visible: hoverArea.containsMouse && labelSummary !== ""
            text: labelSummary
            delay: 500
        }
    }
}
