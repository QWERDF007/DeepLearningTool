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

    property int cellWidth: 330
    property int cellHeight: 250
    property int spacing: 10

    property ItemSelectionModel selection: ProjectManager.currentProject ? ProjectManager.currentProject.imageInstances.selection : null
    property int curImageId: -1
    property var curImageSize

    DltMenu {
        id: imageInstanceMenu
        width: 200
        DltMenuItem {
            text: "删除项目"
            iconSource: DltFontIcon.Delete
            onClicked: {

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
        ScrollBar.vertical: DltScrollBar {}
        model:  ProjectManager.currentProject ? ProjectManager.currentProject.imageInstances : null
        delegate: ImageInstanceDelegate {
            width: instancesView.cellWidth
            height: instancesView.cellHeight
            image.source: model.path? "file:///" + model.path : ""
            image_id: model.image_id ? model.image_id : -1
            selected: model.selected ? model.selected : false
        }

        Keys.onPressed: function(event) {
            let curIndex = selection.currentIndex.row
            let newIndex = curIndex
            let columns = Math.floor(view.width / view.cellWidth)
            let rows = Math.floor(view.height / view.cellHeight)
            if (event.key === Qt.Key_Left) {
                newIndex = Math.max(0, curIndex - 1)
            } else if (event.key === Qt.Key_Right) {
                newIndex = Math.min(view.count - 1, curIndex + 1)
            } else if (event.key === Qt.Key_Up) {
                newIndex = Math.max(0, curIndex - columns)
            } else if (event.key === Qt.Key_Down) {
                newIndex = Math.min(view.count - 1, curIndex + columns)
            } else if (event.key === Qt.Key_Home) {
                // newIndex = 0
                view.positionViewAtBeginning()
            } else if (event.key === Qt.Key_End) {
                // newIndex = view.count - 1
                view.positionViewAtEnd()
            } else if (event.key === Qt.Key_PageUp) {
                let firstVisibleIndex = view.indexAt(0, view.contentY)
                view.positionViewAtIndex(firstVisibleIndex - columns * (rows - 1), GridView.Beginning)
            } else if (event.key === Qt.Key_PageDown) {
                let firstVisibleIndex = view.indexAt(0, view.contentY)
                view.positionViewAtIndex(firstVisibleIndex + columns * (rows - 1), GridView.Beginning)
            }

            if (newIndex !== curIndex) {
                let tmpIndex = view.model.index(newIndex, 0)
                if (event.modifiers & Qt.ShiftModifier) { // shift 多选
                    // view.model.shiftSelect(newIndex, view.lastIndex, ItemSelectionModel.ClearAndSelect)
                } else {
                    if (event.modifiers & Qt.ControlModifier) {

                    } else { // 单选
                        selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
                        selection.setCurrentIndex(tmpIndex, ItemSelectionModel.Select)
                        view.lastIndex = newIndex
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
                            view.model.shiftSelect(index, view.lastIndex, ItemSelectionModel.ClearAndSelect)
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
                    curImageSize = item.image.sourceSize
                } else {
                    selection.clear()
                    curImageId = -1
                    curImageSize = null
                }
            }
        }
    }
}
