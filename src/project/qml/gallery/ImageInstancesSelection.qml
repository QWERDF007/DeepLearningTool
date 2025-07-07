import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle {
    id: imageInstancesSelection
    color: DltColor.Primary
    clip: true

    property Project project: ProjectManager.currentProject
    property ItemSelectionModel selection: project ? project.imageInstances.selection : null
    property int total: selection ? selection.model.count : 0
    property int selected: selection ? selection.selectedIndexes.length : 0
    visible: selection ? selection.hasSelection : false

    RowLayout {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 5
        DltText {
            font: DltFont.Subtitle
            text: "选中:"
            Layout.fillWidth: true
        }
        DltTextIconButton {
            iconSource: DltFontIcon.Clear
            Layout.rightMargin: 5
            text: "清空选择"
            onClicked: {
                if (selection) {
                    selection.clear()
                }
            }
        }
    }
    RowLayout {
        anchors.margins: 10
        anchors.top: title.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        // height: 64
        DltText {
            Layout.fillWidth: true
            text: selected + " / " + total + " 图像"
        }

        DltTextIconButton {
            iconSource: DltFontIcon.Delete
            text: "删除选中图像"
            onClicked: {
                if (project) {
                    project.deleteSelectedImages()
                }
            }
        }
    }

}
