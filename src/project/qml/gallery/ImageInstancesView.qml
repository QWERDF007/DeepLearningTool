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
        cellWidth: instancesView.cellWidth + instancesView.spacing
        cellHeight: instancesView.cellHeight + instancesView.spacing
        ScrollBar.vertical: DltScrollBar {}
        model:  ProjectManager.currentProject ? ProjectManager.currentProject.imageInstances : null
        delegate: ImageInstanceDelegate {
            width: instancesView.cellWidth
            height: instancesView.cellHeight
            image.source: model.path? "file:///" + model.path : ""
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
                    let tmpIndex = view.model.index(index, 0)
                    selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
                    selection.setCurrentIndex(tmpIndex, ItemSelectionModel.ClearAndSelect)
                    if (mouse.button === Qt.RightButton) {
                        imageInstanceMenu.popup()
                    }
                }
            }
        }
    }
}
