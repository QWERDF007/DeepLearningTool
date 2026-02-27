import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.project

import "label"
import "gallery"
import "component"

Rectangle {
    id: labelPage
    width: 1080
    height: 1920
    color: DltColor.Background

    property DataManager dataManager

    // 监听 dataManager 变化
    onDataManagerChanged: {
        console.log("[ReviewPage] dataManager changed:", dataManager);
        if (dataManager) {
            console.log("[ReviewPage] dataManager.labelInstances:", dataManager.labelInstances);
            console.log("[ReviewPage] dataManager.imageLabelsTable:", dataManager.imageLabelsTable);
            if (dataManager.imageLabelsTable) {
                console.log("[ReviewPage] imageLabelsTable.selection:", dataManager.imageLabelsTable.selection);
            }
        }
    }

    Component.onCompleted: {
        console.log("[ReviewPage] Component completed, dataManager:", dataManager);
    }

    DltSplitView {
        anchors.fill: parent
        anchors.margins: 5

        DltSplitView {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 300
            orientation: Qt.Vertical

            DatasetsView { // 数据集
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: DltColor.Primary
                dataManager: labelPage.dataManager
            }

            

            Rectangle { // 类别筛选
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: parent.height / 3
                color: DltColor.Primary
            }

            Rectangle { // 标注列表
                SplitView.fillWidth: true
                SplitView.minimumHeight: 200
                SplitView.preferredHeight: 240
                color: DltColor.Primary
                
            }
        }

        RowLayout {
            SplitView.fillHeight: true
            SplitView.fillWidth: true

            // 标注实例缩略图网格视图
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: DltColor.Background
                
                GridView {
                    id: thumbnailGridView
                    anchors.fill: parent
                    anchors.margins: 10
                    
                    cellWidth: 200
                    cellHeight: 200
                    
                    clip: true
                    
                    // 绑定到所有标注实例
                    model: dataManager ? dataManager.labelInstances : null
                    
                    // 优化：减少缓冲区大小，避免一次性加载太多项目
                    cacheBuffer: 400  // 只缓冲 2 行的高度
                    
                    delegate: Rectangle {
                        width: thumbnailGridView.cellWidth - 10
                        height: thumbnailGridView.cellHeight - 10
                        color: DltColor.Primary
                        radius: 4
                        
                        // 边框：选中时高亮
                        border.width: 2
                        border.color: {
                            if (!dataManager || !dataManager.imageLabelsTable) {
                                return "transparent";
                            }
                            let selection = dataManager.imageLabelsTable.selection;
                            if (!selection || !selection.hasSelection) {
                                return "transparent";
                            }
                            let currentIndex = selection.currentIndex.row;
                            if (currentIndex < 0) {
                                return "transparent";
                            }
                            let selectedData = dataManager.imageLabelsTable.getData(currentIndex);
                            if (selectedData && selectedData.label_id === model.label_id) {
                                return "#2196F3";  // 蓝色高亮
                            }
                            return "transparent";
                        }
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 5
                            spacing: 5
                            
                            // 缩略图
                            LabelInstanceThumbnail {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                labelId: model.label_id
                            }
                            
                            // 标注信息
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 30
                                color: "transparent"
                                
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 2
                                    
                                    DltText {
                                        Layout.fillWidth: true
                                        text: "ID: " + model.label_id
                                        // color: DltColor.TextPrimary
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                    
                                    DltText {
                                        Layout.fillWidth: true
                                        text: model.label_class_name || "Unknown"
                                        // color: DltColor.TextSecondary
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                        
                        // 鼠标点击选中
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (dataManager && dataManager.imageLabelsTable) {
                                    // 找到对应的行索引并选中
                                    for (let i = 0; i < dataManager.imageLabelsTable.rowCount(); i++) {
                                        let data = dataManager.imageLabelsTable.getData(i);
                                        if (data && data.label_id === model.label_id) {
                                            let modelIndex = dataManager.imageLabelsTable.index(i, 0);
                                            dataManager.imageLabelsTable.selection.select(
                                                modelIndex, 
                                                ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows
                                            );
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    // 滚动条
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                }
                
                // 提示文本：当没有标注时显示
                DltText {
                    anchors.centerIn: parent
                    visible: thumbnailGridView.count === 0
                    text: "没有标注实例"
                    // color: DltColor.TextSecondary
                    font.pixelSize: 16
                }
            }

            Rectangle { // 侧边栏
                Layout.preferredWidth: 40
                Layout.fillHeight: true
                color: DltColor.Primary
            }
        }
    }
}
