import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Rectangle {
    id: imageInstancesSelection
    color: QuiColor.Primary
    clip: true

    property DataManager dataManager
    property ImageInstancesModel imageInstances: dataManager ? dataManager.imageInstances : null
    property ItemSelectionModel selection: imageInstances ? imageInstances.selection : null
    property int total: imageInstances ? imageInstances.count : 0
    property int selected: selection ? selection.selectedIndexes.length : 0
    visible: selection ? selection.hasSelection : false

    QuiContentDialog {
        id: deleteConfirmDialog
        title: "删除图像"
        message: "确定删除选中的图像吗?"
        onPositiveClicked: function () {
            if (dataManager) {
                dataManager.deleteSelectedImages()
            }
        }
    }

    RowLayout {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 5
        QuiText {
            font: QuiFont.Subtitle
            text: "选中:"
            Layout.fillWidth: true
        }
        QuiTextIconButton {
            iconSource: QuiFontIcon.Clear
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
        QuiText {
            Layout.fillWidth: true
            text: selected + " / " + total + " 图像"
        }

        QuiTextIconButton {
            iconSource: QuiFontIcon.Delete
            text: "删除选中图像"
            onClicked: {
                    deleteConfirmDialog.open()
            }
        }
    }

}
