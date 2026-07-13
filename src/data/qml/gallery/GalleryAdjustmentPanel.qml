import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.settings
import quickui

import "../label"

Rectangle {
    id: control

    property real imageCellScale: 1.0
    property real imageCellScaleFrom: 0.5
    property real imageCellScaleTo: 4.0
    property real imageCellScaleStepSize: 0.25
    property real imageBrightness: 0.0
    property real imageBrightnessFrom: -1.0
    property real imageBrightnessTo: 1.0
    property real imageBrightnessStepSize: 0.1
    property real imageContrast: 0.0
    property real imageContrastFrom: -1.0
    property real imageContrastTo: 1.0
    property real imageContrastStepSize: 0.1

    color: QuiColor.Primary

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12

        QuiText {
            text: "图库显示："
            font: QuiFont.Subtitle
            Layout.fillWidth: true
        }

        ImageAdjustmentRow {
            Layout.fillWidth: true
            label: "缩放"
            value: control.imageCellScale
            from: control.imageCellScaleFrom
            to: control.imageCellScaleTo
            stepSize: control.imageCellScaleStepSize
            showResetButton: true
            showFitButton: false
            defaultValue: 1.0

            onValueAdjusted: function(newValue) {
                control.setDataField(DataField.CellScale, newValue)
            }

            onResetClicked: {
                control.setDataField(DataField.CellScale, 1.0)
            }
        }

        ImageAdjustmentRow {
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

        ImageAdjustmentRow {
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

        Item {
            Layout.fillHeight: true
        }
    }

    function refreshSettings() {
        imageCellScale = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.CellScale, 1.0)
        let scaleRange = GlobalSettings.valueRangeForField(SettingsAccessor.Data, DataField.CellScale)
        imageCellScaleFrom = scaleRange && scaleRange.length > 0 ? scaleRange[0] : 0.5
        imageCellScaleTo = scaleRange && scaleRange.length > 1 ? scaleRange[1] : 4.0
        imageCellScaleStepSize = scaleRange && scaleRange.length > 2 ? scaleRange[2] : 0.25

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

    function setDataField(field, value) {
        GlobalSettings.setFieldValue(SettingsAccessor.Data, field, value)
    }

    Component.onCompleted: refreshSettings()

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            control.refreshSettings()
        }
    }
}
