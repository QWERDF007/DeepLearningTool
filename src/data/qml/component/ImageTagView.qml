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
    readonly property bool hasSelectedLabels: !multiSelect && imageLabelsList && imageLabelsList.selection
                                             && imageLabelsList.selection.hasSelection
    property int cellWidth: 80
    property int cellHeight: 30
    property int spacing: 10

    property int contextTagId: -1
    property string contextTagName: ""
    property int editingTagId: -1

    QuiMenu {
        id: tagContextMenu
        width: 180

        QuiMenuItem {
            text: "修改 Tag"
            iconSource: QuiFontIcon.Edit
            enabled: imageTagView.contextTagId >= 0
            onClicked: {
                imageTagView.editingTagId = imageTagView.contextTagId
                renameTagDialog.openForm(imageTagView.contextTagName)
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

    DataNameFormDialog {
        id: renameTagDialog
        title: "修改 Tag"
        fieldLabel: "Tag 名称"
        placeholderText: "输入 Tag 名称"
        emptyError: "请输入 Tag 名称"
        nameValidator: function(tagName) {
            if (!imageTagView.dataManager) {
                return "数据管理器不可用"
            }
            if (imageTagView.editingTagId < 0) {
                return "未选择要修改的 Tag"
            }
            return imageTagView.dataManager.isValidTagName(tagName, imageTagView.editingTagId)
        }
        onSubmitted: function(tagName) {
            if (imageTagView.dataManager && imageTagView.editingTagId >= 0) {
                imageTagView.dataManager.updateTagClass(imageTagView.editingTagId, tagName)
            }
            imageTagView.editingTagId = -1
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        ImageTagHeader {
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
                tagStats: imageTagView.multiSelect ? model.selected_images_stats
                                                    : (imageTagView.hasSelectedLabels
                                                       ? model.selected_labels_stats : model.current_image_stats)
                onClicked: {
                    if (imageTagView.multiSelect)
                    {
                        imageTags.setImagesTag(imageInstances.getSelectedImagesId(), model.tag_id)
                    }
                    else if (imageTagView.hasSelectedLabels)
                    {
                        imageTags.setLabelsTag(imageLabelsList.getSelectedLabelIds(), model.tag_id)
                    }
                    else
                    {
                        imageTags.setImageTag(imageInstances.currentImageId, model.tag_id)
                    }
                }
                onContextMenuRequested: function(tagId, tagName) {
                    imageTagView.contextTagId = tagId
                    imageTagView.contextTagName = tagName
                    tagContextMenu.popup()
                }
            }
        }
    }

}
