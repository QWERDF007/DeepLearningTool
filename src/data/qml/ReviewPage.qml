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

                    boundsBehavior:Flickable.StopAtBounds
                    
                    cellWidth: 200
                    cellHeight: 200
                    
                    clip: true
                    
                    // 绑定到所有标注实例
                    model: dataManager ? dataManager.labelInstances : null
                    
                    // 优化：减少缓冲区大小，避免一次性加载太多项目
                    cacheBuffer: 400  // 只缓冲 2 行的高度
                    
                    delegate: Rectangle {
                        id: delegateItem
                        width: thumbnailGridView.cellWidth - 10
                        height: thumbnailGridView.cellHeight - 10
                        color: DltColor.Primary
                        radius: 4
                        
                        property int labelId: model.label_id || -1
                        
                        // 边框：选中时高亮
                        border.width: 2
                        border.color: {
                            if (!dataManager || !dataManager.labelInstances) {
                                return DltColor.Transparent;
                            }
                            let selection = dataManager.labelInstances.selection;
                            if (!selection || !selection.hasSelection) {
                                return DltColor.Transparent
                            }
                            let currentIndex = selection.currentIndex.row;
                            if (currentIndex < 0) {
                                return DltColor.Transparent
                            }
                            // 检查当前 delegate 的索引是否被选中
                            let myIndex = model.index;
                            let myModelIndex = dataManager.labelInstances.index(myIndex, 0);
                            if (selection.isSelected(myModelIndex)) {
                                return DltColor.Highlight
                            }
                            return DltColor.Transparent
                        }
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 5
                            spacing: 5
                            
                            // 缩略图
                            LabelInstanceThumbnail {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                labelId: delegateItem.labelId
                            }
                            
                            // 标注信息
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 30
                                color: DltColor.Transparent
                                
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 2
                                    
                                    DltText {
                                        Layout.fillWidth: true
                                        text: "ID: " + delegateItem.labelId
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
                    }
                    
                    ScrollBar.vertical: DltScrollBar {}

                    
                    // 鼠标点击选中
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        
                        onClicked: function (mouse) {
                            thumbnailGridView.forceActiveFocus()
                            
                            if (!dataManager) {
                                return
                            }
                            
                            if (!dataManager.labelInstances) {
                                return
                            }
                            
                            let selection = dataManager.labelInstances.selection
                            if (!selection) {
                                return
                            }
                            
                            // 计算点击位置对应的索引
                            let posInGridView = Qt.point(mouse.x, mouse.y)
                            let posInContentItem = mapToItem(thumbnailGridView.contentItem, posInGridView)
                            let index = thumbnailGridView.indexAt(posInContentItem.x, posInContentItem.y)
                            
                            if (index < 0) {
                                selection.clear()
                                return
                            }
                            
                            let item = thumbnailGridView.itemAtIndex(index)
                            if (!item) {
                                selection.clear()
                                return
                            }
                            
                            // 直接选中 labelInstances 模型中的对应项
                            let modelIndex = dataManager.labelInstances.index(index, 0)
                            
                            if (mouse.button === Qt.LeftButton) {
                                if (mouse.modifiers & Qt.ControlModifier) {
                                    selection.select(modelIndex, ItemSelectionModel.Toggle)
                                } else {
                                    selection.select(modelIndex, ItemSelectionModel.ClearAndSelect)
                                }
                                selection.setCurrentIndex(modelIndex, ItemSelectionModel.Current)
                            }
                        }
                    }
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
