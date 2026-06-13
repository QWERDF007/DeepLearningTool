import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
    
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 0
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        
        QuiText {
            text: "文件列表:"
            font: QuiFont.Subtitle
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
}
