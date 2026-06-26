import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.settings
import quickui

Rectangle {
    id: sidebar

    property int sidebarKey: SettingsSidebar.Gallery
    property var items: []

    color: QuiColor.Primary

    Component.onCompleted: reloadItems()

    Connections {
        target: GlobalSettings.catalog
        function onCountChanged() { sidebar.reloadItems() }
        function onValueChanged() { sidebar.reloadItems() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 5

        Repeater {
            model: sidebar.items

            QuiTextIconButton {
                property var itemData: modelData

                iconSource: sidebar.iconValue(sidebar.sidebarValue(itemData, "icon", "Settings"))
                text: sidebar.sidebarValue(itemData, "label", itemData.sidebar_label || itemData.name_cn || itemData.name_en)
                onClicked: sidebar.openSlider(itemData, x, y)
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }

    QuiPopup {
        id: popup

        bgColor: QuiColor.Primary
        closePolicy: Popup.CloseOnPressOutside
        width: 200
        height: 32

        property var itemData: null

        QuiSlider {
            id: slider

            anchors.centerIn: parent
            snapMode: Slider.SnapAlways
            onMoved: {
                if (popup.itemData) {
                    GlobalSettings.setCatalogValue(popup.itemData.group_key, popup.itemData.name_en, value)
                }
            }
        }

        T.Overlay.modal: null
    }

    function reloadItems() {
        items = GlobalSettings.catalog.sidebarFieldsFor(sidebarKey)
    }

    function sidebarValue(item, key, fallback) {
        let metadata = item && item.sidebar ? item.sidebar : ({})
        let value = metadata[key]
        return value === undefined || value === null ? fallback : value
    }

    function rangeField(item, key, suffix) {
        let configured = sidebarValue(item, key, "")
        if (configured !== "") {
            return String(configured)
        }
        return String(item.name_en) + suffix
    }

    function numberValue(value, fallback) {
        let next = Number(value)
        return isFinite(next) ? next : fallback
    }

    function settingNumber(item, fieldName, fallback) {
        return numberValue(GlobalSettings.catalog.value(item.group_key, fieldName, fallback), fallback)
    }

    function openSlider(item, x, y) {
        popup.itemData = item

        let valueField = String(item.name_en)
        let fromField = rangeField(item, "from", "_from")
        let toField = rangeField(item, "to", "_to")
        let stepField = rangeField(item, "step", "_step")

        slider.from = settingNumber(item, fromField, 0)
        slider.to = settingNumber(item, toField, 100)
        slider.value = settingNumber(item, valueField, slider.from)
        slider.stepSize = settingNumber(item, stepField, 1)
        slider.snapMode = sidebar.sidebarValue(item, "snap", true) ? Slider.SnapAlways : Slider.NoSnap

        let pos = sidebar.mapToItem(null, x, y)
        popup.x = pos.x - popup.width - 10
        popup.y = pos.y
        popup.open()
    }

    function iconValue(name) {
        switch (String(name)) {
        case "Brightness":
            return QuiFontIcon.Brightness
        case "BlueLight":
            return QuiFontIcon.BlueLight
        case "ExploreContentSingle":
            return QuiFontIcon.ExploreContentSingle
        case "FitPage":
            return QuiFontIcon.FitPage
        case "Zoom":
            return QuiFontIcon.Zoom
        default:
            return QuiFontIcon.Settings
        }
    }
}
