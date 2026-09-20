import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui
import "../component"

SidebarPanelBase {
    id: imageInstancesSelection
    title: "选中:"
    actionText: "清空选择"
    actionIcon: QuiFontIcon.Clear
    onActionClicked: {
        if (selection) {
            selection.clear()
        }
    }

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
        anchors.fill: parent
        anchors.rightMargin: 5

        QuiText {
            Layout.fillWidth: true
            text: imageInstancesSelection.selected + " / " + imageInstancesSelection.total + " 图像"
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
