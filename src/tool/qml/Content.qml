import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project
import dltool.data

StackLayout {
    id: content
    width: 1080
    height: 1920

    property Project project : ProjectManager.currentProject
    property DataManager dataManager: project ? project.dataManager : null

    Component {
        id: project_com
        ProjectPage {
            anchors.fill: parent
            project: content.project
        }
    }

    Component {
        id: dataset_com
        GalleryPage {
            anchors.fill: parent
            dataManager: content.dataManager
        }
    }

    Component {
        id: label_com
        LabelPage {
            anchors.fill: parent
            dataManager: content.dataManager
        }
    }

    Component {
        id: review_com
        ReviewPage {
            anchors.fill: parent
            dataManager: content.dataManager
        }
    }

    property var pages: [project_com, dataset_com, label_com, review_com]

    Repeater {
        model: pages
        Loader {
            sourceComponent: modelData
        }
    }
}
