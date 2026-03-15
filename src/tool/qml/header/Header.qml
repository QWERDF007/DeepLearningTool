import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project
import dltool.data

Rectangle {
    id: header
    width: 600
    height: 80
    color: DltColor.Primary

    property alias currentIndex: mainTabBar.currentIndex
    property var globalFilter: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager.globalFilter : null

    Connections {
        target: SignalHelper
        function onChangeTabBarIndex(index) {
            mainTabBar.currentIndex = index
        }
        
        function onFilterCriteriaChanged(filterType, ids) {
            if (globalFilter) {
                if (filterType === "dataset") {
                    globalFilter.setDatasetFilter(ids)
                } else if (filterType === "tag") {
                    globalFilter.setTagFilter(ids)
                }
            }
        }
        
        function onFilterModuleEnabledChanged(filterType, enabled) {
            if (globalFilter) {
                if (filterType === "dataset") {
                    globalFilter.setDatasetFilterEnabled(enabled)
                } else if (filterType === "tag") {
                    globalFilter.setTagFilterEnabled(enabled)
                }
            }
        }
        
        function onClearAllFilters() {
            if (globalFilter) {
                globalFilter.clearAllFilters()
                // Reset UI state
                datasetDropDown.checked = false
                datasetDropDown.deselectAll()
                tagDropDown.checked = false
                tagDropDown.deselectAll()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        RowLayout {
            Layout.fillWidth: true
            // Layout.fillHeight: true
            Layout.preferredHeight: 36
            MenuTabBar {
                id: mainTabBar
                Layout.fillWidth: true
                Layout.fillHeight: true
                Repeater {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: ["项目", "图库", "标注", "检查", "训练", "评估", "导出"]
                    delegate: DltTabButton {
                        id: tbtn
                        width: 100
                        height: 32
                        text: modelData
                        textColor: mainTabBar.currentIndex === index ? DltColor.Highlight : "white"
                        focusPolicy: Qt.NoFocus
                        enabled: modelData === "项目" || (ProjectManager.currentProject ? true : false)
                    }
                }
            }
            Rectangle { // splitter
                color: DltColor.Background
                width: 5
                Layout.fillHeight: true
            }
            ToolBar {
                Layout.fillHeight: true
                background: Rectangle {
                    color: DltColor.Primary
                }

                RowLayout {
                    anchors.verticalCenter: parent.verticalCenter
                    DltTextIconButton {
                        iconSource: DltFontIcon.Help
                        text: "帮助"
                    }
                    DltTextIconButton {
                        iconSource: DltFontIcon.Settings
                        text: "设置"
                    }
                }
            }
        }
        RowLayout { // TODO: 过滤栏
            id: filterBar
            Layout.fillWidth: true
            Layout.fillHeight: true
            ToolBar {
                Layout.fillHeight: true
                background: Rectangle {
                    color: DltColor.Primary
                }
                RowLayout {
                    DltTextIconButton {
                        iconSource: DltFontIcon.Filter
                        text: globalFilter && globalFilter.isActive 
                              ? "过滤: " + globalFilter.activeFilterCount + " 个条件"
                              : "过滤"
                    }
                    
                    DltTextIconButton {
                        iconSource: DltFontIcon.Clear
                        text: "清除所有"
                        visible: globalFilter && globalFilter.isActive
                        onClicked: SignalHelper.clearAllFilters()
                    }
                    
                    DropDownMenuButton {
                        id: datasetDropDown
                        Layout.fillHeight: true
                        Layout.preferredWidth: 200
                        text: "按数据集: "
                        filterType: "dataset"
                        model: datasetFilterModel
                    }
                    DropDownMenuButton {
                        id: tagDropDown
                        Layout.fillHeight: true
                        Layout.preferredWidth: 200
                        text: "按Tag: "
                        filterType: "tag"
                        model: tagFilterModel
                    }
                }
            }
        }
    }
    
    // Dynamic models populated from DataManager
    ListModel {
        id: datasetFilterModel
    }
    
    ListModel {
        id: tagFilterModel
    }
    
    // Instantiators to populate models from DataManager models
    Instantiator {
        id: datasetInstantiator
        model: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager.datasets : null
        delegate: QtObject {
            Component.onCompleted: {
                datasetFilterModel.append({
                    id: model.dataset_id,
                    text: model.name,
                    checked: true  // Default to checked (select all)
                })
            }
        }
        onObjectAdded: function(index, object) {
            // After all items are added, update the dropdown
            if (index === count - 1) {
                Qt.callLater(function() {
                    datasetDropDown.updateCheckedIds()
                })
            }
        }
    }
    
    Instantiator {
        id: tagInstantiator
        model: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager.imageTags : null
        delegate: QtObject {
            Component.onCompleted: {
                tagFilterModel.append({
                    id: model.tag_id,
                    text: model.name,
                    checked: true  // Default to checked (select all)
                })
            }
        }
        onObjectAdded: function(index, object) {
            // After all items are added, update the dropdown
            if (index === count - 1) {
                Qt.callLater(function() {
                    tagDropDown.updateCheckedIds()
                })
            }
        }
    }
    
    // Clear and repopulate when project changes
    Connections {
        target: ProjectManager
        function onCurrentProjectChanged() {
            datasetFilterModel.clear()
            tagFilterModel.clear()
            
            // 手动填充模型（作为 Instantiator 的备份）
            if (ProjectManager.currentProject && ProjectManager.currentProject.dataManager) {
                let dataManager = ProjectManager.currentProject.dataManager
                
                // 填充数据集模型
                let datasets = dataManager.datasets
                for (let i = 0; i < datasets.rowCount(); i++) {
                    let idx = datasets.index(i, 0)
                    let datasetId = datasets.data(idx, 257)  // DatasetIdRole
                    let datasetName = datasets.data(idx, 258)  // NameRole
                    if (datasetId !== undefined && datasetName !== undefined) {
                        datasetFilterModel.append({
                            id: datasetId,
                            text: datasetName,
                            checked: true  // Default to checked (select all)
                        })
                    }
                }
                
                // 填充标签模型
                let tags = dataManager.imageTags
                for (let i = 0; i < tags.rowCount(); i++) {
                    let idx = tags.index(i, 0)
                    let tagId = tags.data(idx, 257)  // TagIdRole
                    let tagName = tags.data(idx, 258)  // NameRole
                    if (tagId !== undefined && tagName !== undefined) {
                        tagFilterModel.append({
                            id: tagId,
                            text: tagName,
                            checked: true  // Default to checked (select all)
                        })
                    }
                }
                
                // Update the dropdowns after populating
                Qt.callLater(function() {
                    datasetDropDown.updateCheckedIds()
                    tagDropDown.updateCheckedIds()
                })
            }
        }
    }
}
