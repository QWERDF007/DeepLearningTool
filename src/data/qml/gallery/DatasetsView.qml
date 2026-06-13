import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Rectangle {
    id: datasetsView
    width: 200
    height: 200
    color: QuiColor.Primary
    property DataManager dataManager
    property DatasetsModel datasets: dataManager ? dataManager.datasets : null
    property var curItem

    QuiContentDialog {
        id: deleteConfirmDialog
        title: "删除数据集"
        message: "确定删除选中的数据集吗?"
        onPositiveClicked: function () {
            if (dataManager && curItem) {
                dataManager.deleteDataset(curItem.dataset_id)
            }
        }
    }

    ImportDataProgressDialog {
        id: progressDialog
        title: "导入数据"
    }

    QuiMenu {
        id: menu
        width: 200
        QuiMenuItem {
            text: "导入"
            iconSource: QuiFontIcon.ImportMirrored
            onClicked: {
                if (dataManager && curItem) {
                    importDataDialog.dataFormatModel = DataFormat.getSupportedDataFormat()
                    importDataDialog.datasetsModel = dataManager.getAllDatasetsName()
                    importDataDialog.datasetName = curItem.name
                    importDataDialog.open()
                }
            }
        }
        QuiMenuItem {
            text: "导出"
            iconSource: QuiFontIcon.Export
            onClicked: {
                if (dataManager && curItem) {
                    exportDataDialog.dataFormatModel = DataFormat.getSupportedExportDataFormat()
                    exportDataDialog.datasetId = curItem.dataset_id
                    exportDataDialog.datasetName = curItem.name
                    exportDataDialog.open()
                }
            }
        }
        QuiMenuItem {
            text: "修改"
            iconSource: QuiFontIcon.Edit
            onClicked: {
                if (curItem) {
                    editor.text = ""
                    let pos = curItem.mapToItem(null, 0, 0)
                    editor.x = pos.x + curItem.width
                    editor.y = pos.y + 10
                    editor.open()
                }
            }
        }
        QuiMenuItem {
            text: "删除"
            iconSource: QuiFontIcon.Delete
            onClicked: {
                deleteConfirmDialog.open()
            }
        }
    }

    QuiEditor {
        id: editor
        description: "输入数据集名称"
        onEditTextChanged: function (datasetName) {
            if (dataManager && curItem) {
                dataManager.updateDataset(curItem.dataset_id, datasetName)
            }
        }
    }

    ImportDataDialog {
        id: importDataDialog
        dataManager: datasetsView.dataManager
    }

    ExportDataDialog {
        id: exportDataDialog
        dataManager: datasetsView.dataManager
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 0
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        
        DatasetHeader {
            Layout.fillWidth: true
            height: 32
            dataManager: datasetsView.dataManager
        }

        ListView {
            id: view
            clip: true
            Layout.fillHeight: true
            Layout.fillWidth: true
            boundsBehavior: Flickable.StopAtBounds
            model: datasets
            ScrollBar.vertical: QuiScrollBar {}
            delegate: DatasetDelegate {
                height: 32
                width: view.width - 8
                name: model.name
                stats: model.stats
                dataset_id: model.dataset_id
                progress: model.progress
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function (mouse) {
                    if (dataManager === null)
                        return
                    let posInGridView = Qt.point(mouse.x, mouse.y)
                    let posInContentItem = mapToItem(view.contentItem, posInGridView)
                    let index = view.indexAt(posInContentItem.x, posInContentItem.y)
                    let item = view.itemAtIndex(index)
                    if (item) {
                        if (mouse.button === Qt.RightButton) {
                            curItem = item
                            menu.popup()
                        }
                    }
                }
            }
        }
    }
}
