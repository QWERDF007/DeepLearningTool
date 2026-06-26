import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.feature
import dltool.settings
import quickui

import "../component"

// 标注实例缩略图网格视图
Rectangle {
    id: root
    color: QuiColor.Background
    
    property DataManager dataManager
    property LabelInstancesModel labelInstances : dataManager ? dataManager.labelInstances : null
    property ItemSelectionModel selection : labelInstances ? labelInstances.selection : null
    property bool roiSearchEnabled: true
    property real labelThumbnailScale: 1.0
    property real labelThumbnailScaleFrom: 0.5
    property real labelThumbnailScaleTo: 4.0
    property real labelThumbnailScaleStepSize: 0.1
    property real labelThumbnailAspectRatio: 1.0

    QuiContentDialog {
        id: deleteConfirmDialog
        title: "删除标注"
        message: "确定删除选中的标注吗?"
        onPositiveClicked: function () {
            deleteSelectedLabels()
        }
    }

    RoiSearchDialog {
        id: roiSearchDialog
        dataManager: root.dataManager
    }

    QuiMenu {
        id: contextMenu
        width: 200
        QuiMenuItem {
            text: "标注搜索"
            iconSource: QuiFontIcon.Search
            enabled: dataManager && dataManager.imageSearch
                     && selection && selection.hasSelection
                     && !dataManager.imageSearch.running
                     && roiSearchEnabled
            onClicked: startRoiSearchForSelectedLabels()
        }
        QuiMenuItem {
            text: "删除标注"
            iconSource: QuiFontIcon.Delete
            enabled: selection && selection.hasSelection
            onClicked: {
                deleteConfirmDialog.open()
            }
        }
    }
    
    GridView {
        id: thumbnailGridView
        anchors.fill: parent
        anchors.margins: 10
        
        // 基础尺寸
        property real baseCellSize: 200
        
        // 绑定到设置
        cellWidth: baseCellSize * root.labelThumbnailScale
        cellHeight: cellWidth * root.labelThumbnailAspectRatio
        
        clip: true
        
        // 绑定到所有标注实例
        model: labelInstances

        
        // 优化：减少缓冲区大小，避免一次性加载太多项目
        cacheBuffer: 400  // 只缓冲 2 行的高度
        
        keyNavigationEnabled: false // 禁用键盘导航以便启用方向键切换选中
        
        delegate: Rectangle {
            id: delegateItem
            width: thumbnailGridView.cellWidth - 10
            height: thumbnailGridView.cellHeight - 10
            color: QuiColor.Primary
            radius: 4
            
            property int labelId: model.label_id || -1
            property int imageId: model.image_id || -1
            
            // 边框：选中时高亮
            border.width: 2
            border.color: {
                if (!dataManager || !dataManager.labelInstances) {
                    return QuiColor.Transparent
                }
                if (!selection || !selection.hasSelection) {
                    return QuiColor.Transparent
                }
                let currentIndex = selection.currentIndex.row
                if (currentIndex < 0) {
                    return QuiColor.Transparent
                }
                // 检查当前 delegate 的索引是否被选中
                let myIndex = model.index
                let myModelIndex = dataManager.labelInstances.index(myIndex, 0)
                if (selection.isSelected(myModelIndex)) {
                    return QuiColor.Highlight
                }
                return QuiColor.Transparent
            }
            
                
            // 缩略图
            LabelInstanceThumbnail {
                anchors.fill: parent
                anchors.margins: 5
                labelId: delegateItem.labelId
                labelData: model.data || null
                borderColor: model.label_class_color || QuiColor.Transparent
            }
        }
        
        boundsBehavior: Flickable.StopAtBounds
        
        // 滚动条
        ScrollBar.vertical: QuiScrollBar {
            id: scrollBar
        }
        
        // 键盘事件处理
        Keys.enabled: thumbnailGridView.visible
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) {
                selection.clear()
            } else if (event.key === Qt.Key_Delete && selection && selection.hasSelection) {
                deleteConfirmDialog.open()
            } else if ((event.key === Qt.Key_A) && (event.modifiers & Qt.ControlModifier)) {
                if (labelInstances) {
                    labelInstances.selectAll()
                }
            } else {
                updateSelectionByKeyboard(event)
            }
        }
        
        // 缩放视图函数
        function scaleView(event) {
            if (event.angleDelta.y > 0 && root.labelThumbnailScale < root.labelThumbnailScaleTo) {
                root.setDataField(DataField.LabelScale, root.labelThumbnailScale + root.labelThumbnailScaleStepSize)
            } else if (event.angleDelta.y < 0 && root.labelThumbnailScale > root.labelThumbnailScaleFrom) {
                root.setDataField(DataField.LabelScale, root.labelThumbnailScale - root.labelThumbnailScaleStepSize)
            }
        }
        
        // 滚动到当前选中项
        function scrollToCurrentItem() {
            if (selection && selection.hasSelection) {
                let currentIndex = selection.currentIndex.row
                let currentItem = thumbnailGridView.itemAtIndex(currentIndex)
                if (currentItem) {
                    let itemPos = currentItem.mapToItem(thumbnailGridView.contentItem, 0, 0)
                    let itemCenterY = itemPos.y + currentItem.height / 2
                    let targetContentY = itemCenterY - thumbnailGridView.height / 2
                    thumbnailGridView.contentY = Math.max(0, Math.min(targetContentY, thumbnailGridView.contentHeight - thumbnailGridView.height))
                }
            }
        }
        
        // 自定义滚动函数
        function scrollItem(event) {
            const stepFactor = 2.0
            let maxContentY = thumbnailGridView.contentHeight - thumbnailGridView.height
            let delta = event.angleDelta.y / 120
            let step = delta * thumbnailGridView.cellHeight * stepFactor
            let newContentY = thumbnailGridView.contentY - step
            thumbnailGridView.contentY = Math.max(0, Math.min(newContentY, maxContentY))
        }
        
        // 滚轮处理器
        WheelHandler {
            onWheel: function handler(event) {
                if (event.modifiers & Qt.ControlModifier) {
                    thumbnailGridView.scaleView(event)
                    thumbnailGridView.scrollToCurrentItem()
                } else {
                    thumbnailGridView.scrollItem(event)
                }
                event.accepted = true
            }
        }
        
        // 鼠标点击选中
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            
            
            
            onClicked: function (mouse) {
                thumbnailGridView.forceActiveFocus()
                
                if (!labelInstances || !selection) {
                    return
                }
                
                let result = getItemAtMousePosition(mouse)
                
                if (result.index < 0 || !result.item) {
                    selection.clear()
                    return
                }
                
                // 直接选中 labelInstances 模型中的对应项
                let modelIndex = labelInstances.index(result.index, 0)
                
                if (labelInstances.lastIndex === -1) {
                    labelInstances.lastIndex = result.index
                }
                
                if (mouse.button === Qt.LeftButton || (mouse.button === Qt.RightButton && !selection.isSelected(modelIndex))) {
                    if (mouse.modifiers & Qt.ShiftModifier) { // shift 多选
                        labelInstances.shiftSelect(result.index, labelInstances.lastIndex, ItemSelectionModel.ClearAndSelect)
                        selection.setCurrentIndex(modelIndex, ItemSelectionModel.Select)
                    } else if (mouse.modifiers & Qt.ControlModifier) { // ctrl 多选
                        selection.select(modelIndex, ItemSelectionModel.Select)
                        selection.setCurrentIndex(modelIndex, ItemSelectionModel.Select)
                    } else { // 单选
                        selection.select(modelIndex, ItemSelectionModel.ClearAndSelect)
                        selection.setCurrentIndex(modelIndex, ItemSelectionModel.Select)
                        labelInstances.lastIndex = result.index
                    }
                }

                if (mouse.button === Qt.RightButton) {
                    let menuPos = mapToItem(root, mouse.x, mouse.y)
                    contextMenu.x = menuPos.x
                    contextMenu.y = menuPos.y
                    contextMenu.popup()
                }
            }
            
            // Double-click handler for navigation to label page
            onDoubleClicked: function (mouse) {
                if (!labelInstances) {
                    return
                }
                
                let result = getItemAtMousePosition(mouse)
                
                if (result.index < 0 || !result.item) {
                    return
                }
                
                // 获取标注和图像 ID
                let labelId = result.item.labelId
                let imageId = result.item.imageId
                
                if (labelId >= 0 && imageId >= 0) {
                    // Navigate to label page with correct image and label selected
                    SignalHelper.changeTabBarIndex(2)
                    SignalHelper.switchToImage(imageId)
                    SignalHelper.selectLabel(labelId)
                }
            }

            // 计算鼠标位置对应的索引和项目
            function getItemAtMousePosition(mouse) {
                let posInGridView = Qt.point(mouse.x, mouse.y)
                let posInContentItem = mapToItem(thumbnailGridView.contentItem, posInGridView)
                let index = thumbnailGridView.indexAt(posInContentItem.x, posInContentItem.y)
                
                if (index < 0) {
                    return { index: -1, item: null }
                }
                
                let item = thumbnailGridView.itemAtIndex(index)
                return { index: index, item: item }
            }
        }
    }
    
    // 提示文本：当没有标注时显示
    QuiText {
        anchors.centerIn: parent
        visible: thumbnailGridView.count === 0
        text: "没有标注实例"
        font.pixelSize: 16
    }
    
    // 方向键移动选中的函数
    function updateSelectionByKeyboard(event) {
        if (!selection || !labelInstances) {
            return
        }
        
        let curIndex = selection.currentIndex.row
        let newIndex = curIndex
        let columns = Math.floor(thumbnailGridView.width / thumbnailGridView.cellWidth)
        
        // ADWS 上下左右 时计算并选中新项
        if (event.key === Qt.Key_A || event.key === Qt.Key_Left) {
            newIndex = Math.max(0, curIndex - 1)
        } else if (event.key === Qt.Key_D || event.key === Qt.Key_Right) {
            newIndex = Math.min(thumbnailGridView.count - 1, curIndex + 1)
        } else if (event.key === Qt.Key_W || event.key === Qt.Key_Up) {
            newIndex = Math.max(0, curIndex - columns)
        } else if (event.key === Qt.Key_S || event.key === Qt.Key_Down) {
            newIndex = Math.min(thumbnailGridView.count - 1, curIndex + columns)
        } else if (event.key === Qt.Key_Home) {
            thumbnailGridView.positionViewAtBeginning()
        } else if (event.key === Qt.Key_End) {
            thumbnailGridView.positionViewAtEnd()
        } else if (event.key === Qt.Key_PageUp) {
            scrollBar.decrease()
        } else if (event.key === Qt.Key_PageDown) {
            scrollBar.increase()
        }
        
        if (curIndex !== -1 && newIndex !== curIndex) {
            let tmpIndex = labelInstances.index(newIndex, 0)
            selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
            selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
            labelInstances.lastIndex = newIndex
        }
    }

    function deleteSelectedLabels() {
        if (!dataManager || !labelInstances || !selection || !selection.hasSelection) {
            return
        }

        let labelIds = labelInstances.getSelectedLabelIds()
        if (labelIds.length === 0) {
            return
        }
        dataManager.deleteLabels(labelIds)
    }

    function startRoiSearchForSelectedLabels() {
        if (!dataManager || !dataManager.imageSearch || !labelInstances
                || !selection || !selection.hasSelection
                || !roiSearchEnabled) {
            return
        }

        let labelIds = labelInstances.getSelectedLabelIds()
        if (labelIds.length > 0) {
            roiSearchDialog.openForLabels(labelIds)
        }
    }

    function refreshSettings() {
        roiSearchEnabled = GlobalSettings.valueForField(SettingsAccessor.RoiSearch, RoiSearchField.Enabled, true)
        labelThumbnailScale = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.LabelScale, 1.0)
        let scaleRange = GlobalSettings.valueRangeForField(SettingsAccessor.Data, DataField.LabelScale)
        labelThumbnailScaleFrom = scaleRange && scaleRange.length > 0 ? scaleRange[0] : 0.5
        labelThumbnailScaleTo = scaleRange && scaleRange.length > 1 ? scaleRange[1] : 4.0
        labelThumbnailScaleStepSize = scaleRange && scaleRange.length > 2 ? scaleRange[2] : 0.1
        labelThumbnailAspectRatio = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.LabelAspectRatio, 1.0)
    }

    function setDataField(field, value) {
        GlobalSettings.setFieldValue(SettingsAccessor.Data, field, value)
    }

    Component.onCompleted: refreshSettings()

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            root.refreshSettings()
        }
    }
    
    // 监听选中项变化，自动滚动到当前选中项
    Connections {
        target: selection
        function onCurrentIndexChanged(current, previous) {
            thumbnailGridView.positionViewAtIndex(current.row, GridView.Contain)
        }
    }
}
