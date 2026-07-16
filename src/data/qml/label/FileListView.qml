import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

import dltool.ui
import dltool.data
import quickui

Rectangle {
    id: fileListView
    color: QuiColor.Primary
    clip: true
    
    property DataManager dataManager
    property ImageInstancesModel model: dataManager ? dataManager.imageInstances : null
    property ItemSelectionModel selection: model ? model.selection : null
    property int total: model ? model.count : 0
    property int current: selection ? selection.currentIndex.row : -1
    property string contextFileName: ""
    property string contextFilePath: ""

    QuiMenu {
        id: fileContextMenu
        width: 200

        QuiMenuItem {
            text: "复制图像文件名"
            iconSource: QuiFontIcon.Copy
            enabled: fileListView.contextFileName.length > 0
            onTriggered: fileListView.copyText(fileListView.contextFileName)
        }

        QuiMenuItem {
            text: "复制图像文件路径"
            iconSource: QuiFontIcon.Copy
            enabled: fileListView.contextFilePath.length > 0
            onTriggered: fileListView.copyText(fileListView.contextFilePath)
        }

        QuiMenu {
            id: copyToDatasetMenu
            title: "复制到"
            width: 200

            Instantiator {
                model: fileListView.dataManager ? fileListView.dataManager.datasets : null
                delegate: QuiMenuItem {
                    text: model.name
                    iconSource: QuiFontIcon.Folder
                    enabled: fileListView.dataManager && fileListView.selection && fileListView.selection.hasSelection
                    onClicked: {
                        if (fileListView.dataManager && fileListView.model) {
                            fileListView.dataManager.copyToDataset(fileListView.model.getSelectedImagesId(), model.dataset_id)
                        }
                    }
                }
                onObjectAdded: function(index, object) {
                    copyToDatasetMenu.insertItem(index, object)
                }
                onObjectRemoved: function(index, object) {
                    copyToDatasetMenu.removeItem(object)
                }
            }
        }

        QuiMenu {
            id: moveToDatasetMenu
            title: "移动到"
            width: 200

            Instantiator {
                model: fileListView.dataManager ? fileListView.dataManager.datasets : null
                delegate: QuiMenuItem {
                    text: model.name
                    iconSource: QuiFontIcon.Folder
                    enabled: fileListView.dataManager && fileListView.selection && fileListView.selection.hasSelection
                    onClicked: {
                        if (fileListView.dataManager && fileListView.model) {
                            fileListView.dataManager.moveToDataset(fileListView.model.getSelectedImagesId(), model.dataset_id)
                        }
                    }
                }
                onObjectAdded: function(index, object) {
                    moveToDatasetMenu.insertItem(index, object)
                }
                onObjectRemoved: function(index, object) {
                    moveToDatasetMenu.removeItem(object)
                }
            }
        }

        QuiMenuItem {
            text: "删除图像"
            iconSource: QuiFontIcon.Delete
            enabled: fileListView.dataManager !== null && fileListView.current >= 0
            onTriggered: deleteConfirmDialog.open()
        }
    }

    TextEdit {
        id: clipboard
        visible: false
    }

    QuiContentDialog {
        id: deleteConfirmDialog
        title: "删除图像"
        message: fileListView.contextFileName.length > 0
                 ? "确定删除图像 “" + fileListView.contextFileName + "” 吗?"
                 : "确定删除当前图像吗?"
        onPositiveClicked: function() {
            if (fileListView.dataManager) {
                fileListView.dataManager.deleteSelectedImages()
            }
        }
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 0
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        
        RowLayout {
            Layout.fillWidth: true
            QuiText {
                text: "文件列表:"
                font: QuiFont.Subtitle
            }
            Item {
                Layout.fillWidth: true
            }
            QuiText {
                Layout.rightMargin: 10
                text: current >= 0 ? (current + 1) + " / " + total : ""
                font: QuiFont.Subtitle
            }
        }
        
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: QuiScrollBar {}
            model: fileListView.model
            
            delegate: FileListDelegate {
                width: listView.width - 8
                height: 24
                filePath: model.path ? model.path : ""
                selected: model.selected ? model.selected : false
                hasLabels: model.hasLabels ? model.hasLabels : false
                onClicked: fileListView.switchToImage(index)
                onRightClicked: {
                    fileListView.switchToImage(index)
                    fileListView.contextFileName = model.name ? model.name : ""
                    fileListView.contextFilePath = model.path ? model.path : ""
                    fileContextMenu.popup()
                }
            }
            
            // 当前项改变时自动滚动
            Connections {
                target: fileListView.selection
                function onCurrentChanged(current, previous) {
                    if (current.valid) {
                        listView.positionViewAtIndex(current.row, ListView.Contain)
                    }
                }
            }
        }
    }
    
    function switchToImage(index) {
        if (selection && fileListView.model) {
            let newIndex = fileListView.model.index(index, 0)
            selection.select(newIndex, ItemSelectionModel.ClearAndSelect)
            selection.setCurrentIndex(newIndex, ItemSelectionModel.Select)
            fileListView.model.lastIndex = index
        }
    }

    function copyText(text) {
        clipboard.text = text
        clipboard.selectAll()
        clipboard.copy()
    }

    Shortcut {
        enabled: fileListView.visible
        sequences: ["Up", "Left"]
        onActivated: {
            if (selection && total > 0) {
                let row = Math.max(0, current - 1)
                fileListView.switchToImage(row)
            }
        }
    }

    Shortcut {
        enabled: fileListView.visible
        sequences: ["Down", "Right"]
        onActivated: {
            if (selection && total > 0) {
                let row = Math.min(total - 1, current + 1)
                fileListView.switchToImage(row)
            }
        }
    }
}
