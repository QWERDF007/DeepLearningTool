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
                    imageInstance = item
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
                } else {
                    selection.clear()
                }
            }
        }
    }
}
