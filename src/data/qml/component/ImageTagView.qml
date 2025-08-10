import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data

Rectangle {
    id: imageTagView
    color: DltColor.Primary
    property DataManager dataManager
    property ImageInstancesModel imageInstances: dataManager ? dataManager.imageInstances : null
    property ImageTagsModel imageTags: dataManager ? dataManager.imageTags : null
    property bool multiSelect: false
    property int cellWidth: 80
    property int cellHeight: 30
    property int spacing: 10
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
                tagStats: imageTagView.multiSelect ? model.selected_images_stats : model.current_image_stats
                onClicked: {
                    if (imageTagView.multiSelect)
                    {
                        imageTags.setImagesTag(imageInstances.getSelectedImagesId(), model.tag_id)
                    }
                    else
                    {
                        imageTags.setImageTag(imageInstances.currentImageId, model.tag_id)
                    }
                }
            }
        }
    }
}
