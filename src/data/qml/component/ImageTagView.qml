import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Rectangle {
    id: imageTagView
    color: QuiColor.Primary
    property DataManager dataManager
    property ImageInstancesModel imageInstances: dataManager ? dataManager.imageInstances : null
    property ImageLabelsListModel imageLabelsList: dataManager ? dataManager.imageLabelsList : null
    property ImageTagsModel imageTags: dataManager ? dataManager.imageTags : null
    property bool multiSelect: false
    property int _labelSelectionRevision: 0
    readonly property var selectedLabelIds: {
        // Make the binding depend on selection signals as well as hasSelection.
        // A model reset can temporarily leave hasSelection true with no valid rows.
        const revision = _labelSelectionRevision
        if (multiSelect || !imageLabelsList || !imageLabelsList.selection
                || !imageLabelsList.selection.hasSelection) {
            return []
        }
        const ids = imageLabelsList.getSelectedLabelIds()
        return ids ? ids : []
    }
    readonly property bool hasSelectedLabels: selectedLabelIds.length > 0
    property int cellWidth: 80
    property int cellHeight: 30
    property int spacing: 10

    property int contextTagId: -1
    property string contextTagName: ""
    property string contextTagShortcut: ""
    property int editingTagId: -1
    readonly property bool shortcutEditorOpen: renameTagDialog.visible || tagHeader.editorOpen

    Connections {
        target: imageTagView.imageLabelsList ? imageTagView.imageLabelsList.selection : null
        function onSelectionChanged(selected, deselected) {
            ++imageTagView._labelSelectionRevision
        }
        function onCurrentChanged(current, previous) {
            ++imageTagView._labelSelectionRevision
        }
    }

    Connections {
        target: imageTagView.imageLabelsList
        function onModelReset() { ++imageTagView._labelSelectionRevision }
        function onRowsRemoved(parent, first, last) { ++imageTagView._labelSelectionRevision }
    }

    QuiMenu {
        id: tagContextMenu
        width: 180

        QuiMenuItem {
            text: "修改 Tag"
            iconSource: QuiFontIcon.Edit
            enabled: imageTagView.contextTagId >= 0
            onClicked: {
                imageTagView.editingTagId = imageTagView.contextTagId
                renameTagDialog.openForEdit(imageTagView.contextTagId, imageTagView.contextTagName,
                                            imageTagView.contextTagShortcut)
            }
        }

        QuiMenuItem {
            text: "删除 Tag"
            iconSource: QuiFontIcon.Delete
            enabled: imageTagView.contextTagId >= 0
            onClicked: deleteTagConfirmDialog.open()
        }
    }

    QuiContentDialog {
        id: deleteTagConfirmDialog
        title: "删除 Tag"
        message: "删除后将清除所有图像和标注实例上的该 Tag，确定删除吗?"
        onPositiveClicked: {
            if (imageTagView.dataManager && imageTagView.contextTagId >= 0) {
                imageTagView.dataManager.deleteTagClass(imageTagView.contextTagId)
                imageTagView.contextTagId = -1
            }
        }
    }

    ImageTagFormDialog {
        id: renameTagDialog
        dataManager: imageTagView.dataManager
        onSubmitted: function(tagId, tagName, shortcut) {
            if (imageTagView.dataManager && tagId >= 0) {
                imageTagView.dataManager.updateTagClass(tagId, tagName, shortcut)
            }
            imageTagView.editingTagId = -1
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        ImageTagHeader {
            id: tagHeader
            Layout.fillWidth: true
            height: 32
            dataManager: imageTagView.dataManager
        }

        GridView {
            id: view
            Layout.fillHeight: true
            Layout.fillWidth: true
            cellWidth: imageTagView.cellWidth + imageTagView.spacing
            cellHeight: imageTagView.cellHeight + imageTagView.spacing
            model: imageTags
            delegate: ImageTagDelegate {
                width: imageTagView.cellWidth
                height: imageTagView.cellHeight
                tagId: model.tag_id
                tagName: model.name
                tagShortcut: model.shortcut || ""
                tagStats: imageTagView.multiSelect ? model.selected_images_stats
                                                    : (imageTagView.hasSelectedLabels
                                                       ? model.selected_labels_stats : model.current_image_stats)
                onClicked: {
                    imageTagView.toggleTag(Number(model.tag_id))
                }
                onContextMenuRequested: function(tagId, tagName, tagShortcut) {
                    imageTagView.contextTagId = tagId
                    imageTagView.contextTagName = tagName
                    imageTagView.contextTagShortcut = tagShortcut
                    tagContextMenu.popup()
                }
            }
        }
    }

    function toggleTag(tagId) {
        if (!imageTags || !imageInstances || tagId < 0) {
            return false
        }
        if (multiSelect) {
            const imageIds = imageInstances.getSelectedImagesId()
            return imageIds && imageIds.length > 0 ? imageTags.setImagesTag(imageIds, tagId) : false
        }
        if (hasSelectedLabels) {
            return imageTags.setLabelsTag(selectedLabelIds, tagId)
        }
        return imageInstances.currentImageId >= 0
                ? imageTags.setImageTag(imageInstances.currentImageId, tagId) : false
    }

    function handleShortcutEvent(event) {
        if (!event || event.accepted || shortcutEditorOpen || !dataManager || !dataManager.shortcutManager
                || !event.text || event.text.length <= 0
                || (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
            return false
        }
        const tagId = Number(dataManager.shortcutManager.findTagId(event.text))
        return tagId >= 0 ? toggleTag(tagId) : false
    }

}
