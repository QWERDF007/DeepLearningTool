import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.project

Item {
    id: instancesView
    width: 800
    height: 600

    onVisibleChanged: {
        if (visible) {
            view.forceActiveFocus()
        }
    }

    property int cellWidth: 180 * Settings.imageCellScale
    property int cellHeight: 240 * Settings.imageCellScale
    property int spacing: 10

    property Project project: ProjectManager.currentProject
    property ImageInstancesModel model: project ? project.imageInstances : null
    property ItemSelectionModel selection: model ? model.selection : null
    property bool hasSelection: selection ? selection.hasSelection : false

    DltMenu {
        id: imageInstanceMenu
        width: 200
        DltMenuItem {
            text: "删除图像"
            iconSource: DltFontIcon.Delete
            onClicked: {
                if (project) {
                    project.deleteSelected()
                }
            }
        }
    }

    GridView {
        id: view
        clip: true
        anchors.fill: parent
        boundsBehavior: Flickable.StopAtBounds
        cellWidth: instancesView.cellWidth + instancesView.spacing
        cellHeight: instancesView.cellHeight + instancesView.spacing
        ScrollBar.vertical: DltScrollBar {
            id: scrollBar
        }
        keyNavigationEnabled: false // 禁用键盘导航以便启用方向键切换选中图

        model:  project ? project.imageInstances : null
        delegate: ImageInstanceDelegate {
            width: instancesView.cellWidth
            height: instancesView.cellHeight
            image.source: model.path? "file:///" + model.path : ""
            image_id: model.image_id ? model.image_id : -1
            selected: model.selected ? model.selected : false
        }

        Keys.enabled: view.visible // 防止切换页面后还能用按键触发
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) {
                selection.clear()
            } else if (event.key === Qt.Key_Delete) {
                project.deleteSelected()
            } else if ((event.key === Qt.Key_A) && (event.modifiers & Qt.ControlModifier)) {
                project.imageInstances.selectAll()
            } else {
                updateSelectionByKeyboard(event)
            }
        }

        function scaleView(event) {
            if (event.angleDelta.y > 0 && Settings.imageCellScale < Settings.imageCellScaleTo) {
                Settings.imageCellScale += Settings.imageCellScaleStepSize
            } else if (event.angleDelta.y < 0 && Settings.imageCellScale > Settings.imageCellScaleFrom) {
                Settings.imageCellScale -= Settings.imageCellScaleStepSize
            } else {

            }
        }

        function scrollToCurrentItem() {
            // 获取当前项相对于视图内容的位置, 计算视图应该滚动到的位置, 使当前项保持在视图中心
            if (hasSelection) {
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
            // 替换原有的滚动事件, 原来的滚动有点慢, 调整stepFactor系数改变速度
            // 计算滚动步长
            const stepFactor = 2.0
            let maxContentY = view.contentHeight - view.height
            let delta = event.angleDelta.y / 120
            let step = delta * view.cellHeight * stepFactor
            // 计算新的contentY位置并限制范围
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
                if (hasSelection) {
                    SignalHelper.changeTabBarIndex(2)
                }
            }
            
            onClicked: function (mouse) {
                view.forceActiveFocus()
                let posInGridView = Qt.point(mouse.x, mouse.y)
                let posInContentItem = mapToItem(view.contentItem, posInGridView)
                let index = view.indexAt(posInContentItem.x, posInContentItem.y)
                let item = view.itemAtIndex(index)
                if (item) {
                    let tmpIndex = view.model.index(index, 0)
                    if (model.lastIndex === -1) {
                        model.lastIndex = index
                    }
                    if (mouse.button === Qt.LeftButton || (mouse.button === Qt.RightButton && !selection.isSelected(tmpIndex))) {
                        if (mouse.modifiers & Qt.ShiftModifier) { // shift 多选
                            project.imageInstances.shiftSelect(index, model.lastIndex, ItemSelectionModel.ClearAndSelect)
                            selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                        } else if (mouse.modifiers & Qt.ControlModifier) { // ctrl 多选
                            selection.select(tmpIndex, ItemSelectionModel.Select)
                            selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                        } else { // 单选
                            selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
                            selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                            model.lastIndex = index
                        }
                    }
                    if (mouse.button === Qt.RightButton) { // 右键弹出菜单
                        imageInstanceMenu.popup()
                    }
                } else {
                    selection.clear()
                }
            }
        }
    }

    function updateSelectionByKeyboard(event) {
        let curIndex = selection.currentIndex.row
        let newIndex = curIndex
        let columns = Math.floor(view.width / view.cellWidth)
        let rows = Math.floor(view.height / view.cellHeight)
        // ADWS 上下左右 时计算并选中新项
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
            let tmpIndex = view.model.index(newIndex, 0)
            selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
            selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
            model.lastIndex = newIndex
        }
    }

    Connections {
        target: selection
        function onCurrentIndexChanged(current, previous) { 
            view.positionViewAtIndex(current.row, GridView.Contain) // 滚动到当前选中项
        }
    }
}
