import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Templates as T

import dltool.ui
import dltool.data


DltButton {
    id: control
    clip: true

    opacity: enabled ? 1.0 : 0.3

    property alias model : popupModel.model
    property string displayText: ""
    
    // Filter-related properties
    property int filterType: GlobalFilter.FilterType.Dataset  // Use FilterType enum
    property var globalFilter: null  // GlobalFilter instance
    
    // Initialize checkedIds when model changes
    onModelChanged: {
        if (model) {
            Qt.callLater(function() {
                // If nothing is checked on first load, default to "select all"
                if (!control.hasAnyChecked()) {
                    control.selectAll()
                } else {
                    control.updateCheckedIds()
                }
                control.refreshDisplayText()
            })
        } else {
            control.refreshDisplayText()
        }
    }

    Connections {
        target: control.model
        function onDataChanged(topLeft, bottomRight, roles) {
            control.updateCheckedIds()
        }
        function onModelReset() {
            control.updateCheckedIds()
        }
        function onRowsInserted(parent, first, last) {
            control.updateCheckedIds()
        }
        function onRowsRemoved(parent, first, last) {
            control.updateCheckedIds()
        }
        function onLayoutChanged(parents, hint) {
            control.updateCheckedIds()
        }
    }

    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    spacing: 6

    checkable: true
    checked: false  // Default unchecked (filter disabled)

    onCheckedChanged: {
        // When enabling, push criteria first so default "select all" won't be treated as empty criteria.
        if (!globalFilter) {
            console.warn("DropDownMenuButton: globalFilter is null")
            return
        }

        if (checked) {
            let ids = control.getCheckedIds()
            globalFilter.setFilter(filterType, ids)
            globalFilter.setFilterEnabled(filterType, true)
        } else {
            globalFilter.setFilterEnabled(filterType, false)
        }

        control.refreshDisplayText()
    }

    contentItem: RowLayout {
        id: container
        anchors.centerIn: parent
        DltTextIconButton {
            id: dropDownBtn
            iconSource: DltFontIcon.ChevronDown
            Layout.leftMargin: 5
            Layout.alignment: Qt.AlignLeft
            normalColor: Qt.lighter(control.normalColor, 1.2)
            pressedColor: Qt.lighter(control.pressedColor, 1.2)
            hoverColor:  Qt.lighter(control.hoverColor, 1.2)

            onClicked: {
                if (!popup.visible) {
                    openPopup(x, y)
                } else {
                    popup.close()
                }
            }


            DltPopup {
                id: popup
                padding: 6
                bg.border{
                    color: "black"
                    width: 1
                }
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                onOpened: {
                    control.updateCheckedIds()
                }
                
                ColumnLayout {
                    spacing: 5
                    
                    // NEW: Control buttons column (Subtask 9.4)
                    ColumnLayout {
                        spacing: 5
                        Layout.fillWidth: true
                        
                        DltCheckBox {
                            id: selectAllCheckBox
                            text: "全选"
                            checked: true
                            Layout.fillWidth: true
                            
                            onCheckedChanged: {
                                if (checked) {
                                    deselectAllCheckBox.checked = false
                                    control.selectAll()
                                }
                            }
                        }
                        
                        DltCheckBox {
                            id: deselectAllCheckBox
                            text: "全不选"
                            checked: false
                            Layout.fillWidth: true
                            
                            onCheckedChanged: {
                                if (checked) {
                                    selectAllCheckBox.checked = false
                                    control.deselectAll()
                                }
                            }
                        }
                    }
                    
                    // NEW: Separator
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: DltColor.Border
                    }
                    
                    // MODIFIED: Items list
                    ListView {
                        id: popupModel
                        spacing: 5
                        Layout.preferredWidth: 250
                        Layout.preferredHeight: Math.min(contentHeight, 400)
                        clip: true
                        
                        delegate: DltCheckBox {
                            id: itemCheckBox
                            text: model.text
                            width: popupModel.width
                            property bool updatingFromModel: false
                            
                            // Use Component.onCompleted to set initial state
                            Component.onCompleted: {
                                updatingFromModel = true
                                checked = model.checked || false
                                updatingFromModel = false
                            }
                            
                            // Watch for model.checked changes
                            Connections {
                                target: popupModel.model
                                function onDataChanged() {
                                    // Update checkbox when model data changes
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

        DltText {
            text: control.displayText
            font: control.font

            color: control.checked ? DltColor.FontPrimary : control.palette.brightText
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
    
    // NEW: Select all items (Subtask 9.4)
    function selectAll() {
        if (!model) return
        
        for (let i = 0; i < model.rowCount(); i++) {
            let idx = model.index(i, 0)
            model.setData(idx, true, FilterItemsModel.CheckedRole)
        }
        updateCheckedIds()
    }
    
    // NEW: Deselect all items (Subtask 9.4)
    function deselectAll() {
        if (!model) return
        
        for (let i = 0; i < model.rowCount(); i++) {
            let idx = model.index(i, 0)
            model.setData(idx, false, FilterItemsModel.CheckedRole)
        }
        updateCheckedIds()
    }
    
    // NEW: Update checked IDs and emit signal (Subtask 9.4)
    function updateCheckedIds() {
        if (!model) return

        let ids = control.getCheckedIds()
        
        // Update the select all / deselect all checkboxes state
        let checkedCount = ids.length
        let totalCount = model.rowCount()
        
        // Use a flag to prevent triggering onCheckedChanged during programmatic updates
        if (popup.visible) {
            if (checkedCount === totalCount && totalCount > 0) {
                selectAllCheckBox.checked = true
                deselectAllCheckBox.checked = false
            } else if (checkedCount === 0) {
                selectAllCheckBox.checked = false
                deselectAllCheckBox.checked = true
            } else {
                selectAllCheckBox.checked = false
                deselectAllCheckBox.checked = false
            }
        }
        
        // Call GlobalFilter directly to set filter criteria
        if (control.checked && globalFilter) {
            globalFilter.setFilter(filterType, ids)
        } else if (!globalFilter) {
            console.warn("DropDownMenuButton: globalFilter is null")
        }

        control.refreshDisplayText()
    }

    function hasAnyChecked() {
        if (!model) return false

        for (let i = 0; i < model.rowCount(); i++) {
            let idx = model.index(i, 0)
            let isChecked = model.data(idx, FilterItemsModel.CheckedRole)
            if (isChecked) {
                return true
            }
        }
        return false
    }

    function getCheckedIds() {
        if (!model) return []

        let ids = []
        for (let i = 0; i < model.rowCount(); i++) {
            let idx = model.index(i, 0)
            let isChecked = model.data(idx, FilterItemsModel.CheckedRole)
            if (isChecked) {
                let itemId = model.data(idx, FilterItemsModel.IdRole)
                ids.push(itemId)
            }
        }
        return ids
    }
    
    function refreshDisplayText() {
        control.displayText = control.computeDisplayText()
    }

    // NEW: Display checked count and enabled state (Subtask 9.5)
    function computeDisplayText() {
        if (!model) return text + " (未加载)"
        
        // Calculate checked count dynamically
        let checkedCount = 0
        for (let i = 0; i < model.rowCount(); i++) {
            let idx = model.index(i, 0)
            let isChecked = model.data(idx, FilterItemsModel.CheckedRole)
            if (isChecked) {
                checkedCount++
            }
        }
        
        let totalCount = model.rowCount()
        
        if (!control.checked) {
            return text + " (未启用)"
        }
        
        if (checkedCount === 0) {
            return text + " 全不选"
        } else if (checkedCount === totalCount) {
            return text + " 全选"
        } else {
            return text + " " + checkedCount + "/" + totalCount
        }
    }
}
