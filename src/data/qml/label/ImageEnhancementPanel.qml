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
    property real imageBrightness: 0.0
    property real imageBrightnessFrom: -1.0
    property real imageBrightnessTo: 1.0
    property real imageBrightnessStepSize: 0.1
    property real imageContrast: 0.0
    property real imageContrastFrom: -1.0
    property real imageContrastTo: 1.0
    property real imageContrastStepSize: 0.1
    
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
            value: control.imageBrightness
            from: control.imageBrightnessFrom
            to: control.imageBrightnessTo
            stepSize: control.imageBrightnessStepSize
            showResetButton: true
            showFitButton: false
            defaultValue: 0.0
            
            onValueAdjusted: function(newValue) {
                control.setUiField(UiField.Brightness, newValue)
            }
            
            onResetClicked: {
                control.setUiField(UiField.Brightness, 0.0)
            }
        }
        
        // 对比度控制区域
        ImageAdjustmentRow {
            id: contrastControl
            Layout.fillWidth: true
            label: "对比度"
            value: control.imageContrast
            from: control.imageContrastFrom
            to: control.imageContrastTo
            stepSize: control.imageContrastStepSize
            showResetButton: true
            showFitButton: false
            defaultValue: 0.0
            
            onValueAdjusted: function(newValue) {
                control.setUiField(UiField.Contrast, newValue)
            }
            
            onResetClicked: {
                control.setUiField(UiField.Contrast, 0.0)
            }
        }
        
        // 填充剩余空间
        Item {
            Layout.fillHeight: true
        }
    }

    function refreshSettings() {
        imageBrightness = GlobalSettings.valueForField(SettingsAccessor.Ui, UiField.Brightness, 0.0)
        let brightnessRange = GlobalSettings.valueRangeForField(SettingsAccessor.Ui, UiField.Brightness)
        imageBrightnessFrom = brightnessRange && brightnessRange.length > 0 ? brightnessRange[0] : -1.0
        imageBrightnessTo = brightnessRange && brightnessRange.length > 1 ? brightnessRange[1] : 1.0
        imageBrightnessStepSize = brightnessRange && brightnessRange.length > 2 ? brightnessRange[2] : 0.1

        imageContrast = GlobalSettings.valueForField(SettingsAccessor.Ui, UiField.Contrast, 0.0)
        let contrastRange = GlobalSettings.valueRangeForField(SettingsAccessor.Ui, UiField.Contrast)
        imageContrastFrom = contrastRange && contrastRange.length > 0 ? contrastRange[0] : -1.0
        imageContrastTo = contrastRange && contrastRange.length > 1 ? contrastRange[1] : 1.0
        imageContrastStepSize = contrastRange && contrastRange.length > 2 ? contrastRange[2] : 0.1
    }

    function setUiField(field, value) {
        GlobalSettings.setFieldValue(SettingsAccessor.Ui, field, value)
    }

    Component.onCompleted: refreshSettings()

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            control.refreshSettings()
        }
    }
}
