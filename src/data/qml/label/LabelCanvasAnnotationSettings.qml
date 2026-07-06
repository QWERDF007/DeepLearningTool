import QtQuick

import dltool.settings

Item {
    id: settings
    visible: false

    property bool roiSearchEnabled: true
    property int smartAnnotationRefreshInterval: 80
    property real smartAnnotationMaskAlpha: 0.35
    property real labelFillOpacity: 0.3

    signal settingsUpdated()

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            settings.refresh()
            settings.settingsUpdated()
        }
    }

    function refresh() {
        roiSearchEnabled = GlobalSettings.valueForField(SettingsAccessor.RoiSearch, RoiSearchField.Enabled, true)
        smartAnnotationRefreshInterval = GlobalSettings.valueForField(
                    SettingsAccessor.SmartAnnotation,
                    SmartAnnotationField.RefreshInterval,
                    80)
        smartAnnotationMaskAlpha = GlobalSettings.valueForField(
                    SettingsAccessor.SmartAnnotation,
                    SmartAnnotationField.MaskAlpha,
                    0.35)
        labelFillOpacity = Math.max(0, Math.min(1, GlobalSettings.valueForField(
                    SettingsAccessor.Data,
                    DataField.FillOpacity,
                    30) / 100.0))
    }

    Component.onCompleted: refresh()
}
