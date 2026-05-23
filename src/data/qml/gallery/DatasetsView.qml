import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data

Rectangle {
    id: datasetsView
    width: 200
    height: 200
    color: DltColor.Primary
    property DataManager dataManager
    property DatasetsModel datasets: dataManager ? dataManager.datasets : null
    property var curItem

    DltContentDialog {
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

    DltMenu {
        id: menu
        width: 200
        DltMenuItem {
            text: "导入"
            iconSource: DltFontIcon.ImportMirrored
            onClicked: {
                if (dataManager && curItem) {
                    importDataDialog.dataFormatModel = DataFormat.getSupportedDataFormat()
                    importDataDialog.datasetsModel = dataManager.getAllDatasetsName()
                    importDataDialog.datasetName = curItem.name
                    importDataDialog.open()
                }
            }
        }
        DltMenuItem {
            text: "导出"
            iconSource: DltFontIcon.Export
            onClicked: {
                if (dataManager && curItem) {
                    exportDataDialog.dataFormatModel = DataFormat.getSupportedExportDataFormat()
                    exportDataDialog.datasetId = curItem.dataset_id
                    exportDataDialog.datasetName = curItem.name
                    exportDataDialog.open()
                }
            }
        }
        DltMenuItem {
            text: "修改"
            iconSource: DltFontIcon.Edit
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
        DltMenuItem {
            text: "删除"
            iconSource: DltFontIcon.Delete
            onClicked: {
                deleteConfirmDialog.open()
            }
        }
    }

    DltEditor {
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
        anchors.margins: 5
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
            ScrollBar.vertical: DltScrollBar {}
            delegate: DatasetDelegate {
                height: 32
                width: view.width
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
