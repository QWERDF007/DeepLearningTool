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
    
    // NEW: Filter-related properties (Subtask 9.1)
    property string filterType: ""  // "dataset" or "tag"
    property var checkedIds: []     // Track checked item IDs
    
    // Initialize checkedIds when model changes
    onModelChanged: {
        if (model) {
            Qt.callLater(updateCheckedIds)
        }
    }

    //    padding: 0
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    spacing: 6

    // NEW: Make button checkable to control filter enabled state (Subtask 9.1)
    checkable: true
    checked: false  // Default unchecked (filter disabled)

    onCheckedChanged: {
        
        // Notify filter module enabled state changed
        SignalHelper.filterModuleEnabledChanged(filterType, checked)
        
        // If becoming enabled, apply current criteria
        if (checked) {
            SignalHelper.filterCriteriaChanged(filterType, checkedIds)
        }
    }

    contentItem: RowLayout {
        id: container
        //        clip: true
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
                            
                            // NEW: Handle check state change (Subtask 9.4)
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
                // T.Overlay.modal: null // 不显示遮罩
            }
        }

        DltText {
            text: control.updateDisplayText()  // MODIFIED: Use dynamic text (Subtask 9.5)
            font: control.font

            // MODIFIED: Text color changes when checked (Subtask 9.2)
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
        
        let ids = []
        for (let i = 0; i < model.rowCount(); i++) {
            let idx = model.index(i, 0)
            let isChecked = model.data(idx, FilterItemsModel.CheckedRole)
            if (isChecked) {
                let itemId = model.data(idx, FilterItemsModel.IdRole)
                ids.push(itemId)
            }
        }
        checkedIds = ids
        
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
        
        // Only apply filter if button is checked (filter enabled)
        if (control.checked) {
            SignalHelper.filterCriteriaChanged(filterType, ids)
        }
    }
    
    // NEW: Display checked count and enabled state (Subtask 9.5)
    function updateDisplayText() {
        if (!model) return text + " (未加载)"
        
        let checkedCount = checkedIds.length
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
