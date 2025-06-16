import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle {
    id: imageTagView
    color: DltColor.Primary
    property Project project: ProjectManager.currentProject
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
            project: imageTagView.project
        }

        GridView {
            id: view
            Layout.fillHeight: true
            Layout.fillWidth: true
            cellWidth: imageTagView.cellWidth + imageTagView.spacing
            cellHeight: imageTagView.cellHeight + imageTagView.spacing
            model: imageTagView.project ? imageTagView.project.imageTags : null
            delegate: ImageTagDelegate {
                width: imageTagView.cellWidth
                height: imageTagView.cellHeight
                tagId: model.tag_id
                tagName: model.name
                tagStats: imageTagView.multiSelect ? model.selected_images_stats : model.current_image_stats
                onClicked: {
                    if (imageTagView.multiSelect)
                    {
                        view.model.setImagesTag(imageTagView.project.imageInstances.getSelectedImagesId(), model.tag_id)
                    }
                    else
                    {
                        view.model.setImagesTag(imageTagView.project.imageInstances.curImageId, model.tag_id)
                    }
                }
            }
        }
    }
}
