import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle { 
    id: labelImageFlip
    width: 200
    height: 200
    color: DltColor.Primary
    property Project project: ProjectManager.currentProject
    property ItemSelectionModel selection: project ? project.imageInstances.selection : null
    property int total: selection ? selection.model.count : 0
    property int current: selection ? selection.currentIndex.row : -1

    DltMenu {
        id: menu
        width: 200
        DltMenuItem {
            text: "复制"
            onClicked: {
                
            }
        }
    }
    TextEdit {
        id: pasteboard
        visible: false
        text: nameText.text
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 5
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            DltTextIcon {
                iconSize: 32
                iconSource: DltFontIcon.Photo
            }
            DltText {
                id: nameText
                Layout.fillWidth: true
                text: "图像名称"
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        menu.popup()
                    }
                }
            }
            DltTextIconButton {
                iconSource: DltFontIcon.Delete
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            DltTextIconButton {
                iconSource: DltFontIcon.ChevronLeft
            }
            Item {
                Layout.fillWidth: true
            }
            DltText {
                text:current + 1 + "/" + total
            }
            Item {
                Layout.fillWidth: true
            }
            DltTextIconButton {
                iconSource: DltFontIcon.ChevronRight
            }
        }
    }
}
