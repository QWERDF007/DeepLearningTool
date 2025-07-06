import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle { 
    id: labelImageFlip
    clip: true
    width: 200
    height: 200
    color: DltColor.Primary
    property Project project: ProjectManager.currentProject
    property ImageInstancesModel model: project ? project.imageInstances : null
    property ItemSelectionModel selection: model ? model.selection : null
    property int total: model ? model.count : 0
    property int current: selection ? selection.currentIndex.row : -1

    DltMenu {
        id: menu
        width: 200
        DltMenuItem {
            text: "复制"
            onTriggered: {
                copyboard.selectAll()
                copyboard.copy()
            }
        }
    }
    TextEdit {
        id: copyboard
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
                text: model ? model.curImageName : ""
                elide: Text.ElideMiddle
                DltToolTip {
                    text: nameText.text
                    delay: 200
                    visible: mouseArea.containsMouse && nameText.truncated
                }
                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        menu.popup()
                    }
                }
            }
            DltTextIconButton {
                iconSource: DltFontIcon.Delete
                onClicked: {
                    project.deleteSelectedImages()
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            DltTextIconButton {
                iconSource: DltFontIcon.ChevronLeft
                onClicked: {
                    labelImageFlip.prevItem()
                }
            }
            Item {
                Layout.fillWidth: true
            }
            DltText {
                text: current >= 0 ? current + 1 + "/" + total : ""
            }
            Item {
                Layout.fillWidth: true
            }
            DltTextIconButton {
                iconSource: DltFontIcon.ChevronRight
                onClicked: {
                    labelImageFlip.nextItem()
                }
            }
        }
    }

    function prevItem() {
        if (selection && total > 0) {
            let row = Math.max(0, current - 1)
            labelImageFlip.updateIndex(row)
        }
    }

    function nextItem() {
        if (selection && total > 0) {
            let row = Math.min(total - 1, current + 1)
            labelImageFlip.updateIndex(row)
        }
    }

    function updateIndex(row) {
        if (row !== labelImageFlip.current) {
            let newIndex = model.index(row, 0)
            selection.select(newIndex, ItemSelectionModel.ClearAndSelect)
            selection.setCurrentIndex(newIndex, ItemSelectionModel.Select)
            model.lastIndex = row
        }
    }

    Shortcut {
        enabled: labelImageFlip.visible
        sequences: ["a", "left"]
        onActivated: {
            labelImageFlip.prevItem()
        }
    }

    Shortcut {
        enabled: labelImageFlip.visible
        sequences: ["d", "Right"]
        onActivated: {
            labelImageFlip.nextItem()
        }
    }
}
