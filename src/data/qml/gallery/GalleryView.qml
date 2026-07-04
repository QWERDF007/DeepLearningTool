import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

import dltool.ui
import dltool.data
import dltool.feature
import dltool.settings
import quickui

Item {
    id: instancesView
    width: 800
    height: 600

    onVisibleChanged: {
        if (visible) {
            view.forceActiveFocus()
            // Auto-select first image if model has data and nothing is selected (Requirements 1.1, 1.2)
            autoSelectFirstImage()
        }
    }

    function autoSelectFirstImage() {
        if (imageInstances && imageInstances.rowCount() > 0 && selection && !selection.hasSelection) {
            let firstIndex = imageInstances.index(0, 0)
            selection.select(firstIndex, ItemSelectionModel.ClearAndSelect)
            selection.setCurrentIndex(firstIndex, ItemSelectionModel.Select)
        }
    }

    property real imageCellScale: 1.0
    property real imageCellScaleFrom: 0.5
    property real imageCellScaleTo: 4.0
    property real imageCellScaleStepSize: 0.25


    property int cellWidth: 180 * imageCellScale
    property int cellHeight: 240 * imageCellScale
    property int spacing: 10

    property DataManager dataManager
    property FeatureManager featureManager
    property ImageInstancesModel imageInstances: dataManager ? dataManager.imageInstances : null
    property ItemSelectionModel selection: imageInstances ? imageInstances.selection : null
    property ImageSearchController controller: featureManager ? featureManager.imageSearch : null
    property ImageClusterController imageClusterController: featureManager ? featureManager.imageCluster : null

    QuiMenu {
        id: imageInstanceMenu
        width: 200
        QuiMenuItem {
            text: "图像搜索"
            iconSource: QuiFontIcon.Search
            enabled: dataManager && controller && !controller.running && controller.enabled
                     && selection && selection.hasSelection
            onClicked: {
                imageSearchDialog.openForSearch()
            }
        }
        QuiMenuItem {
            text: "图像聚类"
            iconSource: QuiFontIcon.AreaChart
            enabled: dataManager && imageClusterController && !imageClusterController.running
                     && imageClusterController.enabled
                     && selection && selection.hasSelection
            onClicked: {
                if (imageInstances) {
                    imageClusterDialog.openForImages(imageInstances.getSelectedImagesId())
                }
            }
        }
        QuiMenu {
            id: copyToDatasetMenu
            title: "复制到"
            width: 200

            Instantiator {
                model: dataManager ? dataManager.datasets : null
                delegate: QuiMenuItem {
                    text: model.name
                    iconSource: QuiFontIcon.Folder
                    enabled: dataManager && selection && selection.hasSelection
                    onClicked: {
                        if (dataManager && imageInstances) {
                            dataManager.copyToDataset(imageInstances.getSelectedImagesId(), model.dataset_id)
                        }
                    }
                }
                onObjectAdded: function(index, object) {
                    copyToDatasetMenu.insertItem(index, object)
                }
                onObjectRemoved: function(index, object) {
                    copyToDatasetMenu.removeItem(object)
                }
            }
        }
        QuiMenu {
            id: moveToDatasetMenu
            title: "移动到"
            width: 200

            Instantiator {
                model: dataManager ? dataManager.datasets : null
                delegate: QuiMenuItem {
                    text: model.name
                    iconSource: QuiFontIcon.Folder
                    enabled: dataManager && selection && selection.hasSelection
                    onClicked: {
                        if (dataManager && imageInstances) {
                            dataManager.moveToDataset(imageInstances.getSelectedImagesId(), model.dataset_id)
                        }
                    }
                }
                onObjectAdded: function(index, object) {
                    moveToDatasetMenu.insertItem(index, object)
                }
                onObjectRemoved: function(index, object) {
                    moveToDatasetMenu.removeItem(object)
                }
            }
        }
        QuiMenuItem {
            text: "删除图像"
            iconSource: QuiFontIcon.Delete
            onClicked: {
                deleteConfirmDialog.open()
            }
        }
    }

    QuiContentDialog {
        id: deleteConfirmDialog
        title: "删除图像"
        message: "确定删除选中的图像吗?"
        onPositiveClicked: function () {
            if (dataManager) {
                dataManager.deleteSelectedImages()
            }
        }
    }

    ImageSearchDialog {
        id: imageSearchDialog
        dataManager: instancesView.dataManager
        featureManager: instancesView.featureManager
    }

    ImageClusterDialog {
        id: imageClusterDialog
        dataManager: instancesView.dataManager
        featureManager: instancesView.featureManager
    }

    GridView {
        id: view
        clip: true
        anchors.fill: parent
        boundsBehavior: Flickable.StopAtBounds
        cellWidth: instancesView.cellWidth + instancesView.spacing
        cellHeight: instancesView.cellHeight + instancesView.spacing
        cacheBuffer: Math.max(cellHeight * 2, 400)
        ScrollBar.vertical: QuiScrollBar {
            id: scrollBar
        }
        keyNavigationEnabled: false // 禁用键盘导航以便启用方向键切换选中项

        model: imageInstances
        delegate: ImageInstanceDelegate {
            width: instancesView.cellWidth
            height: instancesView.cellHeight
            image.source: model.image_id >= 0
                          ? "image://imageinstance/" + model.image_id
                            + "?w=" + Math.round(width)
                            + "&h=" + Math.round(height)
                          : ""
            image_id: model.image_id >= 0 ? model.image_id : -1
            selected: model.selected ? model.selected : false
            dataManager: instancesView.dataManager
        }

        Keys.enabled: view.visible // 防止切换页面后还能用按键触发
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) {
                selection.clear()
            } else if (event.key === Qt.Key_Delete && selection && selection.hasSelection) {
                deleteConfirmDialog.open()
            } else if ((event.key === Qt.Key_A) && (event.modifiers & Qt.ControlModifier)) {
                imageInstances.selectAll()
            } else {
                updateSelectionByKeyboard(event)
            }
        }

        function scaleView(event) {
            if (event.angleDelta.y > 0 && instancesView.imageCellScale < instancesView.imageCellScaleTo) {
                instancesView.setDataField(DataField.CellScale,
                                           instancesView.imageCellScale + instancesView.imageCellScaleStepSize)
            } else if (event.angleDelta.y < 0 && instancesView.imageCellScale > instancesView.imageCellScaleFrom) {
                instancesView.setDataField(DataField.CellScale,
                                           instancesView.imageCellScale - instancesView.imageCellScaleStepSize)
            } else {

            }
        }

        function scrollToCurrentItem() {
            // 获取当前项相对于视图内容的位置，计算视图应该滚动到的位置，使当前项保持在视图中心
            if (selection && selection.hasSelection) {
                let currentIndex = selection.currentIndex.row
                let currentItem = view.itemAtIndex(currentIndex)
                if (currentItem) {
                    let itemPos = currentItem.mapToItem(view.contentItem, 0, 0)
                    let itemCenterY = itemPos.y + currentItem.height / 2
                    let targetContentY = itemCenterY - view.height / 2
                    view.contentY = Math.max(0, Math.min(targetContentY, view.contentHeight - view.height))
                }
            }
        }

        function scrollItem(event) {
            // 替换原有的滚动事件，原来的滚动有点慢，调整 stepFactor 系数改变速度
            // 计算滚动步长
            const stepFactor = 2.0
            let maxContentY = view.contentHeight - view.height
            let delta = event.angleDelta.y / 120
            let step = delta * view.cellHeight * stepFactor
            // 计算新的 contentY 位置并限制范围
            let newContentY = view.contentY - step
            // 更新位置
            view.contentY = Math.max(0, Math.min(newContentY, maxContentY))
            // scrollBar.position = newContentY / maxContentY
            // view.contentY = newContentY
        }

        WheelHandler {
            // acceptedModifiers: Qt.ControlModifier
            onWheel: function handler(event) {
                if (event.modifiers & Qt.ControlModifier) {
                    view.scaleView(event)
                    view.scrollToCurrentItem()
                } else { 
                    view.scrollItem(event)
                }
                event.accepted = true
            }
        }

        // onCurrentIndexChanged:  {
        //     console.log("onCurrentIndexChanged", currentIndex)
        // }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            onDoubleClicked: function (mouse) {
                if (selection && selection.hasSelection) {
                    SignalHelper.changeTabBarIndex(2)
                }
            }
            
            onClicked: function (mouse) {
                view.forceActiveFocus()
                if (selection === null) {
                    return
                }
                let posInGridView = Qt.point(mouse.x, mouse.y)
                let posInContentItem = mapToItem(view.contentItem, posInGridView)
                let index = view.indexAt(posInContentItem.x, posInContentItem.y)
                let item = view.itemAtIndex(index)
                if (item === null || item === undefined) {
                    selection.clear()
                    return
                }
                let tmpIndex = imageInstances.index(index, 0)
                if (imageInstances.lastIndex === -1) {
                    imageInstances.lastIndex = index
                }
                if (mouse.button === Qt.LeftButton || (mouse.button === Qt.RightButton && !selection.isSelected(tmpIndex))) {
                    if (mouse.modifiers & Qt.ShiftModifier) { // shift 多选
                        imageInstances.shiftSelect(index, imageInstances.lastIndex, ItemSelectionModel.ClearAndSelect)
                        selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                    } else if (mouse.modifiers & Qt.ControlModifier) { // ctrl 多选
                        selection.select(tmpIndex, ItemSelectionModel.Select)
                        selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                    } else { // 单选
                        selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
                        selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                        imageInstances.lastIndex = index
                    }
                }
                if (mouse.button === Qt.RightButton) { // 右键弹出菜单
                    imageInstanceMenu.popup()
                }
            }
        }
    }

    function updateSelectionByKeyboard(event) {
        let curIndex = selection.currentIndex.row
        let newIndex = curIndex
        let columns = Math.floor(view.width / view.cellWidth)
        let rows = Math.floor(view.height / view.cellHeight)
        // WASD/方向键，计算并选中新项
        if (event.key === Qt.Key_A  || event.key === Qt.Key_Left) {
            newIndex = Math.max(0, curIndex - 1)
        } else if (event.key === Qt.Key_D  || event.key === Qt.Key_Right) {
            newIndex = Math.min(view.count - 1, curIndex + 1)
        } else if (event.key === Qt.Key_W || event.key === Qt.Key_Up) {
            newIndex = Math.max(0, curIndex - columns)
        } else if (event.key === Qt.Key_S || event.key === Qt.Key_Down) {
            newIndex = Math.min(view.count - 1, curIndex + columns)
        } else if (event.key === Qt.Key_Home) {
            view.positionViewAtBeginning()
        } else if (event.key === Qt.Key_End) {
            view.positionViewAtEnd()
        } else if (event.key === Qt.Key_PageUp) {
            scrollBar.decrease()
        } else if (event.key === Qt.Key_PageDown) {
            scrollBar.increase()
        }

        if (curIndex !== -1 && newIndex !== curIndex) {
            let tmpIndex = imageInstances.index(newIndex, 0)
            selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
            selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
            imageInstances.lastIndex = newIndex
        }
    }

    function refreshSettings() {
        imageCellScale = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.CellScale, 1.0)
        imageCellScaleFrom = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.CellScaleFrom, 0.5)
        imageCellScaleTo = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.CellScaleTo, 4.0)
        imageCellScaleStepSize = GlobalSettings.valueForField(SettingsAccessor.Data, DataField.CellScaleStep, 0.25)

    }

    function setDataField(field, value) {
        GlobalSettings.setFieldValue(SettingsAccessor.Data, field, value)
    }

    Component.onCompleted: refreshSettings()

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            instancesView.refreshSettings()
        }
    }

    Connections {
        target: selection
        function onCurrentIndexChanged(current, previous) { 
            view.positionViewAtIndex(current.row, GridView.Contain) // 滚动到当前选中项
        }
    }

    // Handle image switching signal from review page navigation
    Connections {
        target: SignalHelper
        function onSwitchToImage(image_id) {
            if (imageInstances && selection) {
                let row = imageInstances.findRowByImageId(image_id)
                if (row >= 0) {
                    let tmpIndex = imageInstances.index(row, 0)
                    selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
                    selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                    imageInstances.lastIndex = row
                    view.positionViewAtIndex(row, GridView.Contain)
                }
            }
        }
    }
}
