import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

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
        QuiText {
            text: control.label
            Layout.preferredWidth: 50
            Layout.alignment: Qt.AlignVCenter
        }

        // 减少按钮
        QuiTextIconButton {
            id: decreaseButton
            iconSource: control.decreaseIconSource()
            iconSize: 16
            text: "减少" + control.label
            enabled: control.value > control.from
            onClicked: {
                var newValue = Math.max(control.from, control.value - control.stepSize)
                control.valueAdjusted(newValue)
            }
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
        }

        // 滑块
        QuiSlider {
            id: slider
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            from: control.from
            to: control.to
            stepSize: control.stepSize
            precision: 2

            // 使用 Binding 保持与外部 value 的同步
            Binding on value {
                value: control.value
                when: !slider.pressed
            }

            onMoved: {
                control.valueAdjusted(slider.value)
            }
        }

        // 增加按钮
        QuiTextIconButton {
            id: increaseButton
            iconSource: control.increaseIconSource()
            iconSize: 16
            text: "增加" + control.label
            enabled: control.value < control.to
            onClicked: {
                var newValue = Math.min(control.to, control.value + control.stepSize)
                control.valueAdjusted(newValue)
            }
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
        }

        // 重置按钮（可选）
        QuiTextIconButton {
            id: resetButton
            visible: control.showResetButton
            iconSource: QuiFontIcon.Refresh
            iconSize: 16
            text: "重置" + control.label
            onClicked: {
                control.valueAdjusted(control.defaultValue)
                control.resetClicked()
            }
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
        }

        // 适应窗口按钮（可选）
        QuiTextIconButton {
            id: fitButton
            visible: control.showFitButton
            iconSource: QuiFontIcon.FitPage
            iconSize: 16
            text: "适应窗口"
            onClicked: {
                control.fitClicked()
            }
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
        }
    }

    function decreaseIconSource() {
        if (label === "缩放") {
            return QuiFontIcon.ZoomOut
        }
        if (label === "亮度") {
            return QuiFontIcon.Brightness
        }
        if (label === "对比度") {
            return QuiFontIcon.Light
        }
        return QuiFontIcon.CalculatorSubtract
    }

    function increaseIconSource() {
        if (label === "缩放") {
            return QuiFontIcon.ZoomIn
        }
        if (label === "亮度") {
            return QuiFontIcon.Brightness
        }
        if (label === "对比度") {
            return QuiFontIcon.Light
        }
        return QuiFontIcon.CalculatorAddition
    }
}
