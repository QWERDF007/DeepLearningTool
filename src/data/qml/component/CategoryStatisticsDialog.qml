import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.qmlmodels

import dltool.ui
import dltool.data

DltPopup {
    id: control
    width: 800
    height: 600
    modal: true
    
    property var dataManager: null
    property var statisticsModel: dataManager ? dataManager.categoryStatisticsModel : null
    property bool applyFilter: false
    property int currentDimension: 0  // 0: instance dimension, 1: image dimension
    
    // TableView properties
    property real rowHeight: 45
    // property real col1Width: 180  // 类别名称
    // property real col2Width: 300  // 分布比例
    // property real col3Width: tableView.width - col1Width - col2Width  // 数量（自动填充）
    
    function open() {
        if (statisticsModel) {
            statisticsModel.refreshData(applyFilter)
        }
        visible = true
    }
    
    function close() {
        visible = false
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 15
        
        // 标题栏
        DltText {
            text: "类别统计"
            font: DltFont.Title
            Layout.fillWidth: true
        }
        
        // 统计表格容器
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: DltColor.Background
            border.color: DltColor.Black
            border.width: 1
            radius: 4
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 0
                spacing: 0
                
                // 表头
                CategoryStatisticsHeader {
                    id: horizontalHeader
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    syncView: tableView
                    currentDimension: control.currentDimension
                    onDimensionChanged: function(dimension) {
                        control.currentDimension = dimension
                    }
                }
                
                // TableView
                TableView {
                    id: tableView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    
                    model: statisticsModel
                    
                    columnWidthProvider: function(column) {
                        if (column === 1)
                            return tableView.width / 2
                        return tableView.width / 4
                    }
                    
                    rowHeightProvider: function(row) {
                        return rowHeight
                    }
                    
                    ScrollBar.vertical: DltScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                    
                    delegate: DelegateChooser {
                        // 第一列：类别名称
                        DelegateChoice {
                            column: 0
                            Rectangle {
                                implicitHeight: rowHeight
                                color: row % 2 === 0 ? DltColor.Background : DltColor.Primary
                                border.color: DltColor.Black
                                border.width: 1
                                
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 15
                                    anchors.rightMargin: 10
                                    spacing: 8
                                    
                                    Rectangle {
                                        Layout.preferredWidth: 30
                                        Layout.preferredHeight: 30
                                        radius: 3
                                        color: model.categoryColor ? model.categoryColor : "#808080"
                                        border.width: 1
                                        border.color: DltColor.Border
                                    }
                                    
                                    DltText {
                                        text: model.categoryName ? model.categoryName : ""
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        color: DltColor.FontPrimary
                                    }
                                }
                            }
                        }
                        
                        // 第二列：分布比例（柱状图）
                        DelegateChoice {
                            column: 1
                            Rectangle {
                                implicitHeight: rowHeight
                                color: row % 2 === 0 ? DltColor.Background : DltColor.Primary
                                border.color: DltColor.Black
                                border.width: 1
                                
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 0
                                    
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 24
                                        color: "transparent"
                                        
                                        Rectangle {
                                            height: 24
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: {
                                                var percentage = control.currentDimension === 0 
                                                    ? (model.instancePercentage || 0)
                                                    : (model.imagePercentage || 0)
                                                return parent.width * percentage
                                            }
                                            color: model.categoryColor ? model.categoryColor : "#808080"
                                            radius: 3
                                        }
                                    }
                                    
                                    DltText {
                                        Layout.preferredWidth: 60
                                        text: {
                                            var percentage = control.currentDimension === 0 
                                                ? (model.instancePercentage || 0)
                                                : (model.imagePercentage || 0)
                                            return (percentage * 100).toFixed(1) + "%"
                                        }
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }
                        }
                        
                        // 第三列：数量
                        DelegateChoice {
                            column: 2
                            Rectangle {
                                implicitHeight: rowHeight
                                color: row % 2 === 0 ? DltColor.Background : DltColor.Primary
                                border.color: DltColor.Black
                                border.width: 1
                                
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 15
                                    anchors.rightMargin: 15
                                    spacing: 0
                                    
                                    Item { // 添加一个容器来避免文本内容的实际渲染宽度不同，可能会导致视觉上的不对齐
                                        Layout.fillHeight: true
                                        Layout.fillWidth: true
                                        DltText {
                                            anchors.fill: parent
                                            text: model.instanceCount || 0
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                    Item {
                                        Layout.fillHeight: true
                                        Layout.fillWidth: true
                                        DltText {
                                            anchors.fill: parent
                                            text: model.imageCount || 0
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // 底部按钮栏
        RowLayout {
            Layout.fillWidth: true
            
            DltCheckBox {
                id: filterCheckBox
                text: "应用筛选条件"
                checked: control.applyFilter
                onCheckedChanged: {
                    control.applyFilter = checked
                    if (statisticsModel) {
                        statisticsModel.refreshData(applyFilter)
                    }
                }
            }
            
            Item {
                Layout.fillWidth: true
            }
            
            DltButton {
                text: "关闭"
                onClicked: {
                    control.close()
                }
            }
        }
    }
}
