import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle {
    id: control
    
    // 组件属性
    property real zoomValue: 1.0
    property real zoomFrom: 0.25
    property real zoomTo: 32.0
    property real zoomStepSize: 0.1
    
    // 信号定义
    signal fitToWindow()
    signal zoomChanged(real zoom)
    
    color: DltColor.Primary
    
    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12
        
        // 标题
        DltText {
            text: "图像增强："
            font: DltFont.Subtitle
            Layout.fillWidth: true
        }
        
        // 缩放控制区域
        ImageAdjustmentRow {
            id: zoomControl
            Layout.fillWidth: true
            label: "缩放"
            value: control.zoomValue
            from: control.zoomFrom
            to: control.zoomTo
            stepSize: control.zoomStepSize
            showResetButton: false
            showFitButton: true
            
            onValueAdjusted: function(newValue) {
                control.zoomChanged(newValue)
            }
            
            onFitClicked: {
                control.fitToWindow()
            }
        }
        
        // 亮度控制区域
        ImageAdjustmentRow {
            id: brightnessControl
            Layout.fillWidth: true
            label: "亮度"
            value: Settings.imageBrightness
            from: Settings.imageBrightnessFrom
            to: Settings.imageBrightnessTo
            stepSize: Settings.imageBrightnessStepSize
            showResetButton: true
            showFitButton: false
            defaultValue: 0.0
            
            onValueAdjusted: function(newValue) {
                Settings.imageBrightness = newValue
            }
            
            onResetClicked: {
                Settings.imageBrightness = 0.0
            }
        }
        
        // 对比度控制区域
        ImageAdjustmentRow {
            id: contrastControl
            Layout.fillWidth: true
            label: "对比度"
            value: Settings.imageContrast
            from: Settings.imageContrastFrom
            to: Settings.imageContrastTo
            stepSize: Settings.imageContrastStepSize
            showResetButton: true
            showFitButton: false
            defaultValue: 0.0
            
            onValueAdjusted: function(newValue) {
                Settings.imageContrast = newValue
            }
            
            onResetClicked: {
                Settings.imageContrast = 0.0
            }
        }
        
        // 填充剩余空间
        Item {
            Layout.fillHeight: true
        }
    }
}
