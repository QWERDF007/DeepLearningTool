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

    property int cellWidth: 320 * Settings.imageCellScale + 10
    property int cellHeight: 240 * Settings.imageCellScale + 10
    property int spacing: 10

    property Project project: ProjectManager.currentProject
    property ItemSelectionModel selection: project ? project.imageInstances.selection : null
    property int curImageId: -1
    property bool hasSelection: selection ? selection.hasSelection : false

    DltMenu {
        id: imageInstanceMenu
        width: 200
        DltMenuItem {
            text: "删除项目"
            iconSource: DltFontIcon.Delete
            onClicked: {
                if (project) {
                    project.imageInstances.deleteSelected()
                }
            }
        }
    }

    GridView {
        id: view
        clip: true
        anchors.fill: parent
        property int lastIndex: -1
        cellWidth: instancesView.cellWidth + instancesView.spacing
        cellHeight: instancesView.cellHeight + instancesView.spacing
        ScrollBar.vertical: DltScrollBar {
            id: scrollBar
        }
        model:  project ? project.imageInstances : null
        delegate: ImageInstanceDelegate {
            width: instancesView.cellWidth
            height: instancesView.cellHeight
            image.source: model.path? "file:///" + model.path : ""
            image_id: model.image_id ? model.image_id : -1
            selected: model.selected ? model.selected : false
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) {
                selection.clear()
                curImageId = -1
            } else if (event.key === Qt.Key_Delete) {
                project.imageInstances.deleteSelected()
            } else if ((event.key === Qt.Key_A) && (event.modifiers & Qt.ControlModifier)) {
                project.imageInstances.selectAll()
            } else {
                updateSelectionByKeyboard(event)
            }
        }

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: function handler(event) {
                if (event.modifiers) {
                    if (event.angleDelta.y > 0 && Settings.imageCellScale < Settings.imageCellScaleTo) {
                        Settings.imageCellScale += Settings.imageCellScaleStep
                    } else if (event.angleDelta.y < 0 && Settings.imageCellScale > Settings.imageCellScaleFrom) {
                        Settings.imageCellScale -= Settings.imageCellScaleStep
                    } else {

                    }
                }
            }
        }

        // onCurrentIndexChanged:  {
        //     console.log("onCurrentIndexChanged", currentIndex)
        // }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: function (mouse) {
                view.forceActiveFocus()
                let posInGridView = Qt.point(mouse.x, mouse.y)
                let posInContentItem = mapToItem(view.contentItem, posInGridView)
                let index = view.indexAt(posInContentItem.x, posInContentItem.y)
                let item = view.itemAtIndex(index)
                if (item) {
                    let tmpIndex = view.model.index(index, 0)
                    if (view.lastIndex === -1) {
                        view.lastIndex = index
                    }
                    if (mouse.button === Qt.RightButton) { // 右键弹出菜单
                        imageInstanceMenu.popup()
                    } else if (mouse.button === Qt.LeftButton) { // 选中
                        if (mouse.modifiers & Qt.ShiftModifier) { // shift 多选
                            project.imageInstances.shiftSelect(index, view.lastIndex, ItemSelectionModel.ClearAndSelect)
                            selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                        } else if (mouse.modifiers & Qt.ControlModifier) { // ctrl 多选
                            selection.select(tmpIndex, ItemSelectionModel.Select)
                            selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                        } else { // 单选
                            selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
                            selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                            view.lastIndex = index
                        }
                    }
                    curImageId = item.image_id
                } else {
                    selection.clear()
                    curImageId = -1
                }
            }
        }
    }

    function updateSelectionByKeyboard(event) {
        let curIndex = selection.currentIndex.row
        let newIndex = curIndex
        let columns = Math.floor(view.width / view.cellWidth)
        let rows = Math.floor(view.height / view.cellHeight)
        if (event.key === Qt.Key_A) {
            newIndex = Math.max(0, curIndex - 1)
        } else if (event.key === Qt.Key_D) {
            newIndex = Math.min(view.count - 1, curIndex + 1)
        } else if (event.key === Qt.Key_W) {
            newIndex = Math.max(0, curIndex - columns)
        } else if (event.key === Qt.Key_S) {
            newIndex = Math.min(view.count - 1, curIndex + columns)
        } else if (event.key === Qt.Key_Home) {
            // newIndex = 0
            view.positionViewAtBeginning()
        } else if (event.key === Qt.Key_End) {
            // newIndex = view.count - 1
            view.positionViewAtEnd()
        } else if (event.key === Qt.Key_PageUp) {
            scrollBar.decrease()
            // let firstVisibleIndex = view.indexAt(0, view.contentY)
            // view.positionViewAtIndex(firstVisibleIndex - columns * (rows - 1), GridView.Beginning)
        } else if (event.key === Qt.Key_PageDown) {
            scrollBar.increase()
        }

        if (curIndex !== -1 && newIndex !== curIndex) {
            let tmpIndex = view.model.index(newIndex, 0)
            selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
            selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
            view.lastIndex = newIndex
            view.positionViewAtIndex(newIndex, GridView.Contain)
            let item = view.itemAtIndex(newIndex)
            if (item) {
                curImageId = item.image_id
            }
            
        }
    }
}
