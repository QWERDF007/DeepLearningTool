import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.settings
import quickui

Rectangle {
    id: control
    
    // 组件属性
    property real zoomValue: 1.0
    property real zoomFrom: 0.25
    property real zoomTo: 32.0
    property real zoomStepSize: 0.1
    readonly property var uiSettings: GlobalSettings.settingsObjectFor(SettingsAccessor.Ui)
    
    // 信号定义
    signal fitToWindow()
    signal zoomChanged(real zoom)
    
    color: QuiColor.Primary
    
    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12
        
        // 标题
        QuiText {
            text: "图像增强："
            font: QuiFont.Subtitle
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
            value: uiSettings.imageBrightness
            from: uiSettings.imageBrightnessFrom
            to: uiSettings.imageBrightnessTo
            stepSize: uiSettings.imageBrightnessStepSize
            showResetButton: true
            showFitButton: false
            defaultValue: 0.0
            
            onValueAdjusted: function(newValue) {
                uiSettings.imageBrightness = newValue
            }
            
            onResetClicked: {
                uiSettings.imageBrightness = 0.0
            }
        }
        
        // 对比度控制区域
        ImageAdjustmentRow {
            id: contrastControl
            Layout.fillWidth: true
            label: "对比度"
            value: uiSettings.imageContrast
            from: uiSettings.imageContrastFrom
            to: uiSettings.imageContrastTo
            stepSize: uiSettings.imageContrastStepSize
            showResetButton: true
            showFitButton: false
            defaultValue: 0.0
            
            onValueAdjusted: function(newValue) {
                uiSettings.imageContrast = newValue
            }
            
            onResetClicked: {
                uiSettings.imageContrast = 0.0
            }
        }
        
        // 填充剩余空间
        Item {
            Layout.fillHeight: true
        }
    }
}
