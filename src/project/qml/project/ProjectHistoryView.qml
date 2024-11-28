import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.project

Item {
    id: projectsView
    width: 800
    height: 600
    // color: DltColor.Background

    property int cellWidth: 210
    property int cellHeight: 170
    property int spacing: 10
    property string path: ""
    property ItemSelectionModel selection: ProjectManager.recentProjects ? ProjectManager.recentProjects.selection : null

    DltMenu {
        id: menu
        width: 200
        // onWidthChanged: {
        //     console.log("menu width changed", width, childrenRect.width)
        // }

        DltMenuItem {
            text: "打开项目"

            // onWidthChanged: {
            //     console.log("打开项目", width)
            // }
        }
        DltMenuItem {
            text: "删除项目"
            // onWidthChanged: {
            //     console.log("删除项目", width)
            // }
        }
        DltMenuItem {
            text: "从最近项目列表中删除"
            iconSource: DltFontIcon.DeleteWord
        }
        DltMenuItem {
            text: "在资源浏览器中打开"
            iconSource: DltFontIcon.OpenLocal
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        DltText {
            text: "最近项目:"
        }

        GridView {
            id: view
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: DltScrollBar {}
            Layout.fillHeight: true
            Layout.fillWidth: true
            cellHeight: projectsView.cellHeight + projectsView.spacing
            cellWidth: projectsView.cellWidth + projectsView.spacing
            // interactive: false
            model: ProjectManager.recentProjects
            delegate: ProjectDelegate {
                width: projectsView.cellWidth
                height: projectsView.cellHeight
                selected: model.selected
                name: model.name
                path: model.path
                msg: model.tooltip
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                propagateComposedEvents: true
                onClicked: function (mouse) {
                    let posInGridView = Qt.point(mouse.x, mouse.y)
                    let posInContentItem = mapToItem(view.contentItem, posInGridView)
                    let index = view.indexAt(posInContentItem.x, posInContentItem.y)
                    let item = view.itemAtIndex(index)
                    if (item) {
                        selection.select(view.model.index(index, 0), ItemSelectionModel.ClearAndSelect)
                        projectsView.path = item.path
                        if (mouse.button === Qt.RightButton) {
                            menu.popup()
                        }
                    }
                }
            }
        }
    }
}
