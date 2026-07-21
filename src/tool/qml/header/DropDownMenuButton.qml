import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import dltool.ui
import dltool.data
import quickui

QuiButton {
    id: control
    clip: true

    opacity: enabled ? 1.0 : 0.3

    property alias model: popupModel.model
    property string displayText: ""
    property int filterType: GlobalFilter.FilterType.Dataset
    property var globalFilter: null
    property bool showItemList: true
    property int resultCount: -1
    property bool selectAllByDefault: true

    property bool initialized: false
    property bool bulkUpdating: false
    property bool syncingBulkControls: false
    property bool suppressCheckedHandler: false
    property bool allDeselected: false

    onTextChanged: refreshDisplayText()
    onResultCountChanged: refreshDisplayText()

    onGlobalFilterChanged: {
        if (showItemList) {
            Qt.callLater(syncFromGlobalFilter)
        }
    }

    onFilterTypeChanged: {
        if (showItemList) {
            Qt.callLater(syncFromGlobalFilter)
        }
    }

    onModelChanged: {
        if (!showItemList) {
            refreshDisplayText()
            return
        }

        if (model) {
            Qt.callLater(function() {
                if (control.selectAllByDefault && !control.initialized && !control.hasAnyChecked()) {
                    control.setAllModelChecked(true)
                    control.allDeselected = false
                }
                control.initialized = true
                if (control.globalFilter && control.globalFilter.isFilterEnabled(control.filterType)) {
                    control.syncFromGlobalFilter()
                } else {
                    control.updateCheckedIds()
                }
            })
        } else {
            control.initialized = false
            control.refreshDisplayText()
        }
    }

    Connections {
        target: control.showItemList ? control.globalFilter : null
        function onFilterStateChanged() {
            control.syncFromGlobalFilter()
        }
    }

    Connections {
        target: control.showItemList ? control.model : null
        ignoreUnknownSignals: true
        function onDataChanged(topLeft, bottomRight, roles) {
            if (!control.bulkUpdating) {
                control.updateCheckedIds()
            }
        }
        function onModelReset() {
            if (!control.bulkUpdating) {
                control.updateCheckedIds()
            }
        }
        function onRowsInserted(parent, first, last) {
            if (!control.bulkUpdating) {
                control.updateCheckedIds()
            }
        }
        function onRowsRemoved(parent, first, last) {
            if (!control.bulkUpdating) {
                control.updateCheckedIds()
            }
        }
        function onLayoutChanged(parents, hint) {
            if (!control.bulkUpdating) {
                control.updateCheckedIds()
            }
        }
    }

    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    spacing: 6
    checkable: true
    checked: false

    onCheckedChanged: {
        if (suppressCheckedHandler) {
            refreshDisplayText()
            return
        }

        if (!globalFilter) {
            console.warn("DropDownMenuButton: globalFilter is null")
            return
        }

        if (checked) {
            control.applyCurrentSelection()
            globalFilter.setFilterEnabled(filterType, true)
        } else {
            globalFilter.setFilterEnabled(filterType, false)
        }

        control.refreshDisplayText()
    }

    contentItem: RowLayout {
        id: container
        anchors.centerIn: parent

        QuiTextIconButton {
            id: dropDownBtn
            iconSource: QuiFontIcon.ChevronDown
            Layout.leftMargin: 5
            Layout.alignment: Qt.AlignLeft
            normalColor: Qt.lighter(control.normalColor, 1.2)
            pressedColor: Qt.lighter(control.pressedColor, 1.2)
            hoverColor: Qt.lighter(control.hoverColor, 1.2)

            onClicked: {
                if (!popup.visible) {
                    control.openPopup(x, y)
                } else {
                    popup.close()
                }
            }

            QuiPopup {
                id: popup
                padding: 6
                bg.border {
                    color: "black"
                    width: 1
                }
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                onOpened: {
                    control.updateCheckedIds()
                }

                ColumnLayout {
                    spacing: 5

                    ColumnLayout {
                        spacing: 5
                        Layout.fillWidth: true

                        QuiCheckBox {
                            id: selectAllCheckBox
                            text: "全选"
                            checked: true
                            Layout.fillWidth: true

                            onClicked: {
                                if (!control.syncingBulkControls) {
                                    control.selectAll()
                                }
                            }
                        }

                        QuiCheckBox {
                            id: deselectAllCheckBox
                            text: "全不选"
                            checked: false
                            Layout.fillWidth: true

                            onClicked: {
                                if (!control.syncingBulkControls) {
                                    control.deselectAll()
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: control.showItemList ? 1 : 0
                        visible: control.showItemList
                        color: QuiColor.Border
                    }

                    ListView {
                        id: popupModel
                        spacing: 5
                        Layout.preferredWidth: 250
                        Layout.preferredHeight: control.showItemList ? Math.min(contentHeight, 400) : 0
                        visible: control.showItemList
                        clip: true

                        delegate: QuiCheckBox {
                            id: itemCheckBox
                            text: model.text
                            width: popupModel.width
                            enabled: model.enabled
                            property bool updatingFromModel: false

                            Component.onCompleted: {
                                updatingFromModel = true
                                checked = model.checked || false
                                updatingFromModel = false
                            }

                            Connections {
                                target: popupModel.model
                                ignoreUnknownSignals: true
                                function onDataChanged() {
                                    itemCheckBox.updatingFromModel = true
                                    itemCheckBox.checked = model.checked || false
                                    itemCheckBox.updatingFromModel = false
                                }
                            }

                            onCheckedChanged: {
                                if (!updatingFromModel && model.checked !== checked) {
                                    model.checked = checked
                                    control.updateCheckedIds()
                                }
                            }
                        }
                    }
                }

                maskVisible: false
            }
        }

        QuiText {
            text: control.displayText
            font: control.font
            color: control.checked ? QuiColor.FontPrimary : control.palette.brightText
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            fontSizeMode: Text.Fit
            Layout.rightMargin: 5
        }
    }

    function openPopup(x, y) {
        let pos = control.mapToItem(null, x, y)
        popup.x = pos.x
        popup.y = pos.y + 30
        popup.open()
    }

    function warnMissingGlobalFilter() {
        if (!globalFilter) {
            console.warn("DropDownMenuButton: globalFilter is null")
            return true
        }
        return false
    }

    function ensureFilterEnabled() {
        if (warnMissingGlobalFilter()) {
            return
        }

        if (!control.checked) {
            control.suppressCheckedHandler = true
            control.checked = true
            control.suppressCheckedHandler = false
        }
        globalFilter.setFilterEnabled(filterType, true)
    }

    function syncFromGlobalFilter() {
        if (!control.showItemList) {
            control.refreshDisplayText()
            return
        }

        if (!globalFilter) {
            control.refreshDisplayText()
            return
        }

        let filterEnabled = globalFilter.isFilterEnabled(filterType)
        control.suppressCheckedHandler = true
        control.checked = filterEnabled
        control.suppressCheckedHandler = false

        if (filterEnabled && model) {
            control.setModelCheckedIds(globalFilter.getActiveIds(filterType),
                                       globalFilter.isFilterInverted(filterType))
        }

        control.syncBulkControlsFromModel()
        control.refreshDisplayText()
    }

    function setAllModelChecked(checked) {
        if (!model) {
            return
        }

        control.bulkUpdating = true
        for (let i = 0; i < model.rowCount(); i++) {
            let idx = model.index(i, 0)
            if (model.data(idx, FilterItemsModel.EnabledRole)) {
                model.setData(idx, checked, FilterItemsModel.CheckedRole)
            }
        }
        control.bulkUpdating = false
    }

    function setModelCheckedIds(ids, inverted) {
        if (!model) {
            return
        }

        control.bulkUpdating = true
        for (let i = 0; i < model.rowCount(); i++) {
            let idx = model.index(i, 0)
            let id = model.data(idx, FilterItemsModel.IdRole)
            let matched = control.containsId(ids, id)
            let itemEnabled = model.data(idx, FilterItemsModel.EnabledRole)
            model.setData(idx, itemEnabled && (inverted ? !matched : matched), FilterItemsModel.CheckedRole)
        }
        control.bulkUpdating = false
    }

    function selectAll(applyFilter) {
        let shouldApply = applyFilter !== false
        control.allDeselected = false
        if (control.showItemList) {
            control.setAllModelChecked(true)
        }
        control.setBulkControls(true, false)

        if (shouldApply && !warnMissingGlobalFilter()) {
            globalFilter.selectAll(filterType)
            control.ensureFilterEnabled()
        }
        control.refreshDisplayText()
    }

    function deselectAll(applyFilter) {
        let shouldApply = applyFilter !== false
        control.allDeselected = true
        if (control.showItemList) {
            control.setAllModelChecked(false)
        }
        control.setBulkControls(false, true)

        if (shouldApply && !warnMissingGlobalFilter()) {
            globalFilter.deselectAll(filterType)
            control.ensureFilterEnabled()
        }
        control.refreshDisplayText()
    }

    function updateCheckedIds() {
        if (!control.showItemList) {
            control.setBulkControls(!control.allDeselected, control.allDeselected)
            control.refreshDisplayText()
            return
        }

        control.syncBulkControlsFromModel()

        if (control.checked && globalFilter) {
            control.applyCurrentSelection()
        }

        control.refreshDisplayText()
    }

    function syncBulkControlsFromModel() {
        if (!model) {
            return
        }

        let checkedCount = control.getCheckedCount()
        let totalCount = control.getEnabledCount()

        if (totalCount === 0) {
            control.setBulkControls(!control.allDeselected, control.allDeselected)
        } else if (checkedCount === totalCount) {
            control.allDeselected = false
            control.setBulkControls(true, false)
        } else if (checkedCount === 0) {
            control.allDeselected = true
            control.setBulkControls(false, true)
        } else {
            control.allDeselected = false
            control.setBulkControls(false, false)
        }
    }

    function applyCurrentSelection() {
        if (warnMissingGlobalFilter()) {
            return
        }

        if (!control.showItemList) {
            if (control.allDeselected) {
                globalFilter.deselectAll(filterType)
            } else {
                globalFilter.selectAll(filterType)
            }
            return
        }

        if (!model) {
            return
        }

        let checkedCount = control.getCheckedCount()
        let totalCount = control.getEnabledCount()
        if (totalCount === 0) {
            if (control.allDeselected) {
                globalFilter.deselectAll(filterType)
            } else {
                globalFilter.selectAll(filterType)
            }
        } else if (checkedCount === 0) {
            globalFilter.deselectAll(filterType)
        } else if (checkedCount === totalCount && totalCount > 0) {
            globalFilter.selectAll(filterType)
        } else {
            globalFilter.setFilter(filterType, control.getCheckedIds())
        }
    }

    function setBulkControls(selectChecked, deselectChecked) {
        control.syncingBulkControls = true
        selectAllCheckBox.checked = selectChecked
        deselectAllCheckBox.checked = deselectChecked
        control.syncingBulkControls = false
    }

    function hasAnyChecked() {
        if (!model) {
            return false
        }

        for (let i = 0; i < model.rowCount(); i++) {
            let idx = model.index(i, 0)
            if (model.data(idx, FilterItemsModel.EnabledRole)
                    && model.data(idx, FilterItemsModel.CheckedRole)) {
                return true
            }
        }
        return false
    }

    function getCheckedCount() {
        if (!model) {
            return 0
        }

        let checkedCount = 0
        for (let i = 0; i < model.rowCount(); i++) {
            let idx = model.index(i, 0)
            if (model.data(idx, FilterItemsModel.EnabledRole)
                    && model.data(idx, FilterItemsModel.CheckedRole)) {
                checkedCount++
            }
        }
        return checkedCount
    }

    function getEnabledCount() {
        if (!model) {
            return 0
        }

        let enabledCount = 0
        for (let i = 0; i < model.rowCount(); i++) {
            let idx = model.index(i, 0)
            if (model.data(idx, FilterItemsModel.EnabledRole)) {
                enabledCount++
            }
        }
        return enabledCount
    }

    function getCheckedIds() {
        if (!model) {
            return []
        }

        let ids = []
        for (let i = 0; i < model.rowCount(); i++) {
            let idx = model.index(i, 0)
            if (model.data(idx, FilterItemsModel.EnabledRole)
                    && model.data(idx, FilterItemsModel.CheckedRole)) {
                ids.push(model.data(idx, FilterItemsModel.IdRole))
            }
        }
        return ids
    }

    function containsId(ids, id) {
        for (let i = 0; i < ids.length; i++) {
            if (ids[i] === id) {
                return true
            }
        }
        return false
    }

    function refreshDisplayText() {
        control.displayText = control.computeDisplayText()
    }

    function computeDisplayText() {
        if (!control.checked) {
            return text + " (未启用)"
        }

        if (!control.showItemList) {
            let countText = control.resultCount >= 0 ? " " + control.resultCount : ""
            return text + countText + (control.allDeselected ? " 全不选" : " 全选")
        }

        if (!model) {
            return text + " (未加载)"
        }

        let checkedCount = control.getCheckedCount()
        let totalCount = control.getEnabledCount()

        if (totalCount === 0) {
            return text + (control.allDeselected ? " 全不选" : " 全选")
        } else if (checkedCount === 0) {
            return text + " 全不选"
        } else if (checkedCount === totalCount) {
            return text + " 全选"
        }
        return text + " " + checkedCount + "/" + totalCount
    }
}
