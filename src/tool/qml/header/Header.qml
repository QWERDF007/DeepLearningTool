import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import dltool.ui
import dltool.project
import dltool.data
import dltool.feature
import dltool.settings
import quickui

Rectangle {
    id: header
    width: 600
    height: 80
    color: QuiColor.Primary

    property alias currentIndex: mainTabBar.currentIndex
    property var globalFilter: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager.globalFilter : null
    property FeatureManager featureManager: ProjectManager.currentProject ? ProjectManager.currentProject.featureManager : null
    property var imageSearch: featureManager ? featureManager.imageSearch : null
    property var smartAnnotation: featureManager ? featureManager.smartAnnotation : null

    onGlobalFilterChanged: syncFileNameFilterField()

    function syncFileNameFilterField() {
        const nextText = globalFilter ? globalFilter.fileNameFilterText : ""
        if (fileNameFilterField.text !== nextText) {
            fileNameFilterField.text = nextText
        }
    }

    Connections {
        target: SignalHelper
        function onChangeTabBarIndex(index) {
            mainTabBar.currentIndex = index
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            MenuTabBar {
                id: mainTabBar
                Layout.fillWidth: true
                Layout.fillHeight: true
                Repeater {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: ["项目", "图库", "标注", "检查", "训练", "评估", "导出"]
                    delegate: QuiTabButton {
                        id: tbtn
                        width: 100
                        height: 32
                        text: modelData
                        textColor: mainTabBar.currentIndex === index ? QuiColor.Highlight : QuiColor.FontPrimary
                        focusPolicy: Qt.NoFocus
                        enabled: modelData === "项目" || (ProjectManager.currentProject ? true : false)
                    }
                }
            }
            ToolBar {
                Layout.fillHeight: true
                background: Rectangle {
                    color: QuiColor.Primary
                }

                RowLayout {
                    anchors.verticalCenter: parent.verticalCenter
                    QuiTextIconButton {
                        iconSource: QuiFontIcon.AreaChart
                        text: "统计"
                        enabled: ProjectManager.currentProject !== null
                        onClicked: {
                            categoryStatisticsDialog.open()
                        }
                    }
                    QuiTextIconButton {
                        iconSource: QuiFontIcon.AllApps
                        text: "任务"
                        enabled: ProjectManager.currentProject !== null
                        onClicked: {
                            taskCenterWindow.open()
                        }
                    }
                }
            }
            Rectangle { // splitter
                color: QuiColor.Background
                width: 5
                Layout.fillHeight: true
            }
            ToolBar {
                Layout.fillHeight: true
                background: Rectangle {
                    color: QuiColor.Primary
                }

                RowLayout {
                    anchors.verticalCenter: parent.verticalCenter
                    QuiTextIconButton {
                        iconSource: QuiFontIcon.Help
                        text: "帮助"
                    }
                    QuiTextIconButton {
                        iconSource: QuiFontIcon.Settings
                        text: "设置"
                        onClicked: settingsDialog.open()
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
                    color: QuiColor.Primary
                }
                RowLayout {
                    QuiTextIconButton {
                        iconSource: QuiFontIcon.Filter
                        text: globalFilter && globalFilter.isActive 
                              ? "过滤: " + globalFilter.activeFilterCount + " 个条件"
                              : "过滤"
                    }
                    
                    QuiTextIconButton {
                        iconSource: QuiFontIcon.Clear
                        text: "清除所有"
                        visible: globalFilter && globalFilter.isActive
                        onClicked: {
                            if (globalFilter) {
                                globalFilter.clearAllFilters()
                                // Reset UI state
                                datasetDropDown.checked = false
                                datasetDropDown.selectAll(false)
                                tagDropDown.checked = false
                                tagDropDown.selectAll(false)
                                labelClassImageDropDown.checked = false
                                labelClassImageDropDown.selectAll(false)
                                customFilterDropDown.checked = false
                                customFilterDropDown.deselectAll(false)
                                fileNameFilterField.text = ""
                            }
                        }
                    }

                    

                    DropDownMenuButton {
                        id: datasetDropDown
                        Layout.fillHeight: true
                        Layout.preferredWidth: 200
                        text: "按数据集: "
                        filterType: GlobalFilter.FilterType.Dataset
                        globalFilter: header.globalFilter
                        model: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager.datasetFilterItems : null
                    }
                    DropDownMenuButton {
                        id: tagDropDown
                        Layout.fillHeight: true
                        Layout.preferredWidth: 200
                        text: "按Tag: "
                        filterType: GlobalFilter.FilterType.Tag
                        globalFilter: header.globalFilter
                        model: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager.tagFilterItems : null
                    }
                    DropDownMenuButton {
                        id: labelClassImageDropDown
                        Layout.fillHeight: true
                        Layout.preferredWidth: 200
                        text: "按标签类别: "
                        filterType: GlobalFilter.FilterType.ImageLabelClass
                        globalFilter: header.globalFilter
                        model: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager.labelClassFilterItems : null
                    }

                    DropDownMenuButton {
                        id: customFilterDropDown
                        Layout.fillHeight: true
                        Layout.preferredWidth: 200
                        text: "自定义过滤"
                        filterType: GlobalFilter.FilterType.Custom
                        globalFilter: header.globalFilter
                        model: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager.customFilterItems : null
                        selectAllByDefault: false
                    }

                    QuiTextField {
                        id: fileNameFilterField
                        Layout.fillHeight: true
                        Layout.preferredWidth: 180
                        placeholderText: "按文件名过滤"
                        enabled: globalFilter !== null
                        selectByMouse: true
                        onTextChanged: {
                            if (globalFilter && text !== globalFilter.fileNameFilterText) {
                                globalFilter.fileNameFilterText = text
                            }
                        }
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
            labelClassImageDropDown.checked = false
            customFilterDropDown.checked = false
            header.syncFileNameFilterField()
        }
    }

    Connections {
        target: header.globalFilter
        ignoreUnknownSignals: true
        function onFilterStateChanged() {
            header.syncFileNameFilterField()
        }
    }
    
    CategoryStatisticsDialog {
        id: categoryStatisticsDialog
        dataManager: ProjectManager.currentProject ? ProjectManager.currentProject.dataManager : null
    }

    TaskCenterWindow {
        id: taskCenterWindow
        transientParent: header.Window.window
    }

    SettingsDialog {
        id: settingsDialog
        transientParent: header.Window.window
        imageSearch: header.imageSearch
        smartAnnotation: header.smartAnnotation
    }
}
