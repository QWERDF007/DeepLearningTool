import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.settings
import quickui

Rectangle {
    id: control

    property real labelThumbnailScale: 1.0
    property real labelThumbnailScaleFrom: 0.5
    property real labelThumbnailScaleTo: 4.0
    property real labelThumbnailScaleStepSize: 0.1
    property real labelThumbnailAspectRatio: 1.0
    property real labelThumbnailAspectRatioFrom: 0.5
    property real labelThumbnailAspectRatioTo: 2.0
    property real labelThumbnailAspectRatioStepSize: 0.1
    property real labelThumbnailBorderPadding: 10.0
    property real labelThumbnailBorderPaddingFrom: 0.0
    property real labelThumbnailBorderPaddingTo: 200.0
    property real labelThumbnailBorderPaddingStepSize: 10.0
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
            text: "缩略图增强："
            font: QuiFont.Subtitle
            Layout.fillWidth: true
        }

        ImageAdjustmentRow {
            Layout.fillWidth: true
            label: "缩放"
            value: control.labelThumbnailScale
            from: control.labelThumbnailScaleFrom
            to: control.labelThumbnailScaleTo
            stepSize: control.labelThumbnailScaleStepSize
            showResetButton: true
            showFitButton: false
            defaultValue: 1.0

            onValueAdjusted: function(newValue) {
                control.setDataField(DataField.LabelScale, newValue)
            }

            onResetClicked: {
                control.setDataField(DataField.LabelScale, 1.0)
            }
        }

        ImageAdjustmentRow {
            Layout.fillWidth: true
            label: "宽高比"
            value: control.labelThumbnailAspectRatio
            from: control.labelThumbnailAspectRatioFrom
            to: control.labelThumbnailAspectRatioTo
            stepSize: control.labelThumbnailAspectRatioStepSize
            showResetButton: true
            showFitButton: false
            defaultValue: 1.0

            onValueAdjusted: function(newValue) {
                control.setDataField(DataField.LabelAspectRatio, newValue)
            }

            onResetClicked: {
                control.setDataField(DataField.LabelAspectRatio, 1.0)
            }
        }

        ImageAdjustmentRow {
            Layout.fillWidth: true
            label: "留白"
            value: control.labelThumbnailBorderPadding
            from: control.labelThumbnailBorderPaddingFrom
            to: control.labelThumbnailBorderPaddingTo
            stepSize: control.labelThumbnailBorderPaddingStepSize
            showResetButton: true
            showFitButton: false
            defaultValue: 10.0

            onValueAdjusted: function(newValue) {
                control.setDataField(DataField.LabelBorderPadding, newValue)
            }

            onResetClicked: {
                control.setDataField(DataField.LabelBorderPadding, 10.0)
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
        labelThumbnailScale = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.LabelScale, 1.0)
        let scaleRange = GlobalSettings.valueRangeForField(SettingsAccessor.Data, DataField.LabelScale)
        labelThumbnailScaleFrom = scaleRange && scaleRange.length > 0 ? scaleRange[0] : 0.5
        labelThumbnailScaleTo = scaleRange && scaleRange.length > 1 ? scaleRange[1] : 4.0
        labelThumbnailScaleStepSize = scaleRange && scaleRange.length > 2 ? scaleRange[2] : 0.1

        labelThumbnailAspectRatio = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.LabelAspectRatio, 1.0)
        let aspectRatioRange = GlobalSettings.valueRangeForField(SettingsAccessor.Data, DataField.LabelAspectRatio)
        labelThumbnailAspectRatioFrom = aspectRatioRange && aspectRatioRange.length > 0 ? aspectRatioRange[0] : 0.5
        labelThumbnailAspectRatioTo = aspectRatioRange && aspectRatioRange.length > 1 ? aspectRatioRange[1] : 2.0
        labelThumbnailAspectRatioStepSize = aspectRatioRange && aspectRatioRange.length > 2 ? aspectRatioRange[2] : 0.1

        labelThumbnailBorderPadding = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.LabelBorderPadding, 10.0)
        let borderPaddingRange = GlobalSettings.valueRangeForField(SettingsAccessor.Data, DataField.LabelBorderPadding)
        labelThumbnailBorderPaddingFrom = borderPaddingRange && borderPaddingRange.length > 0 ? borderPaddingRange[0] : 0.0
        labelThumbnailBorderPaddingTo = borderPaddingRange && borderPaddingRange.length > 1 ? borderPaddingRange[1] : 200.0
        labelThumbnailBorderPaddingStepSize = borderPaddingRange && borderPaddingRange.length > 2 ? borderPaddingRange[2] : 10.0

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
