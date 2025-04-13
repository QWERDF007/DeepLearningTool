import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle {
    id: datasetsView
    width: 200
    height: 200
    color: DltColor.Primary
    property Project project: ProjectManager.currentProject
    property var curItem

    DltMenu {
        id: menu
        width: 200
        DltMenuItem {
            text: "导入"
            iconSource: DltFontIcon.ImportMirrored
            onClicked: {
                if (project && curItem) {
                    importDataDialog.datasetsModel = project.getAllDatasetsName()
                    importDataDialog.datasetName = curItem.name
                    importDataDialog.open()
                }
            }
        }
        DltMenuItem {
            text: "修改"
            iconSource: DltFontIcon.Edit
            onClicked: {
                if (curItem) {
                    editor.text = ""
                    let pos = datasetsView.mapToItem(Qt.application.activeWindow, curItem.x, curItem.y)
                    editor.x = pos.x + curItem.width
                    editor.y = pos.y + 60
                    editor.open()
                }
            }
        }
        DltMenuItem {
            text: "删除"
            iconSource: DltFontIcon.Delete
            onClicked: {
                if (project && curItem) {
                    project.deleteDataset(curItem.dataset_id)
                }
            }
        }
    }

    DltEditor {
        id: editor
        description: "输入数据集名称"
        onEditTextChanged: function (datasetName) {
            if (project && curItem) {
                project.updateDataset(curItem.dataset_id, datasetName)
            }
        }
    }

    ImportDataDialog {
        id: importDataDialog
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        DatasetHeader {
            Layout.fillWidth: true
            height: 32
            project: datasetsView.project
        }

        ListView {
            id: view
            clip: true
            Layout.fillHeight: true
            Layout.fillWidth: true
            model: datasetsView.project ? datasetsView.project.datasets : null
            ScrollBar.vertical: DltScrollBar {}
            delegate: DatasetDelegate {
                height: 32
                width: view.width
                name: model.name
                stats: model.stats
                dataset_id: model.dataset_id
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function (mouse) {
                    if (datasetsView.project === null)
                        return
                    let posInGridView = Qt.point(mouse.x, mouse.y)
                    let posInContentItem = mapToItem(view.contentItem, posInGridView)
                    let index = view.indexAt(posInContentItem.x, posInContentItem.y)
                    let item = view.itemAtIndex(index)
                    if (item) {
                        // let tmpIndex = view.model.index(index, 0)
                        if (mouse.button === Qt.RightButton) {
                            curItem = item
                            // cur_dataset_id = item.dataset_id
                            // cur_dataset_name = item.name
                            menu.popup()
                        }
                    }
                }
            }
        }
    }
}
