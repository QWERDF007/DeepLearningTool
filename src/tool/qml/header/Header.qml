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
                        model: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager.datasetFilterItems : null
                    }
                    DropDownMenuButton {
                        id: tagDropDown
                        Layout.fillHeight: true
                        Layout.preferredWidth: 200
                        text: "按Tag: "
                        filterType: "tag"
                        model: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager.tagFilterItems : null
                    }
                }
            }
        }
    }
    
    // Clear filter UI state when project changes
    Connections {
        target: ProjectManager
        function onCurrentProjectChanged() {
            // Reset dropdown UI state
            datasetDropDown.checked = false
            tagDropDown.checked = false
        }
    }
}
