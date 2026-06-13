import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.project
import quickui

Item {
    id: projectsView
    width: 800
    height: 600
    // color: QuiColor.Background

    property int cellWidth: 210
    property int cellHeight: 170
    property int spacing: 10
    property Project project: ProjectManager.currentProject
    property string path: ProjectManager.recentProjects ? ProjectManager.recentProjects.currentProjectPath : ""
    property ItemSelectionModel selection: ProjectManager.recentProjects ? ProjectManager.recentProjects.selection : null

    QuiMenu {
        id: projectMenu
        width: 200

        QuiMenuItem {
            text: "打开项目"
            iconSource: QuiFontIcon.OpenFolderHorizontal
            onClicked: {
                ProjectManager.openProject(projectsView.path)
            }
        }
        QuiMenuItem {
            text: "删除项目"
            iconSource: QuiFontIcon.Delete
            onClicked: {
                deleteConfirmDialog.open()
            }
        }
        QuiMenuItem {
            text: "从最近项目列表中删除"
            enabled: project ? project.path !== projectsView.path : true

            iconSource: QuiFontIcon.Cancel
            onClicked: {
                ProjectManager.removeFromRectentProjects(projectsView.path)
            }
        }
        QuiMenuItem {
            text: "在资源浏览器中打开"
            iconSource: QuiFontIcon.OpenLocal
            onClicked: {
                Utils.openInFileExplorer(projectsView.path)
            }
        }
    }

    QuiContentDialog {
        id: deleteConfirmDialog
        title: "删除项目"
        message: "确定删除选中的项目吗?"
        onPositiveClicked: function () {
            if (ProjectManager) {
                ProjectManager.deleteProject(projectsView.path)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        QuiText {
            text: "最近项目:"
        }

        GridView {
            id: view
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: QuiScrollBar {}
            Layout.fillHeight: true
            Layout.fillWidth: true
            cellHeight: projectsView.cellHeight + projectsView.spacing
            cellWidth: projectsView.cellWidth + projectsView.spacing
            // interactive: false
            model: ProjectManager.recentProjects
            delegate: ProjectDelegate {
                width: projectsView.cellWidth
                height: projectsView.cellHeight
                selected: model.selected ? model.selected : false
                name: model.name ? model.name : ""
                path: model.path ? model.path : ""
                msg: model.tooltip ? model.tooltip : ""
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
                        let tmpIndex = view.model.index(index, 0)
                        selection.select(tmpIndex, ItemSelectionModel.ClearAndSelect)
                        selection.setCurrentIndex(tmpIndex, ItemSelectionModel.ClearAndSelect)
                        if (mouse.button === Qt.RightButton) {
                            projectMenu.popup()
                        }
                    }
                }
            }
        }
    }
}
