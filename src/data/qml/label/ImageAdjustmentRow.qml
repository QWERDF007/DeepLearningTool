import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Item {
    id: control
    
    // 属性定义
    property string label: ""
    property real value: 0.0
    property real from: 0.0
    property real to: 1.0
    property real stepSize: 0.1
    property bool showResetButton: true
    property bool showFitButton: false
    property real defaultValue: 0.0
    
    // 信号定义
    signal valueAdjusted(real newValue)
    signal resetClicked()
    signal fitClicked()
    
    implicitWidth: rowLayout.implicitWidth
    implicitHeight: rowLayout.implicitHeight
    
    RowLayout {
        id: rowLayout
        anchors.fill: parent
        spacing: 4
        
        // 标签
        DltText {
            text: control.label
            Layout.preferredWidth: 50
            Layout.alignment: Qt.AlignVCenter
        }
        
        // 减少按钮
        DltTextIconButton {
            id: decreaseButton
            iconSource: control.label === "缩放" ? DltFontIcon.ZoomOut : 
                       (control.label === "亮度" ? DltFontIcon.Brightness : DltFontIcon.Light)
            iconSize: 16
            text: "减少" + control.label
            enabled: control.value > control.from
            onClicked: {
                var newValue = Math.max(control.from, control.value - control.stepSize)
                control.value = newValue
                control.valueAdjusted(newValue)
            }
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
        }
        
        // 滑动条
        DltSlider {
            id: slider
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            from: control.from
            to: control.to
            stepSize: control.stepSize
            value: control.value
            precision: 2
            
            onMoved: {
                control.value = value
                control.valueAdjusted(value)
            }
        }
        
        // 增加按钮
        DltTextIconButton {
            id: increaseButton
            iconSource: control.label === "缩放" ? DltFontIcon.Zoom : 
                       (control.label === "亮度" ? DltFontIcon.Brightness : DltFontIcon.Light)
            iconSize: 16
            text: "增加" + control.label
            enabled: control.value < control.to
            onClicked: {
                var newValue = Math.min(control.to, control.value + control.stepSize)
                control.value = newValue
                control.valueAdjusted(newValue)
            }
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
        }
        
        // 重置按钮（可选）
        DltTextIconButton {
            id: resetButton
            visible: control.showResetButton
            iconSource: DltFontIcon.Refresh
            iconSize: 16
            text: "重置" + control.label
            onClicked: {
                control.value = control.defaultValue
                control.valueAdjusted(control.defaultValue)
                control.resetClicked()
            }
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
        }
        
        // 适应窗口按钮（可选）
        DltTextIconButton {
            id: fitButton
            visible: control.showFitButton
            iconSource: DltFontIcon.FitPage
            iconSize: 16
            text: "适应窗口"
            onClicked: {
                control.fitClicked()
            }
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
        }
    }
}
