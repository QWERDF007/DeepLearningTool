import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project
import dltool.data
import dltool.model

StackLayout {
    id: content
    width: 1080
    height: 1920

    property Project project : ProjectManager.currentProject
    property DataManager dataManager: project ? project.dataManager : null
    property bool shuttingDown: false

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

    Component {
        id: train_com
        TrainPage {
            anchors.fill: parent
        }
    }

    Component {
        id: test_com
        TestPage {
            anchors.fill: parent
        }
    }

    property var pages: [project_com, dataset_com, label_com, review_com, train_com, test_com]

    Repeater {
        model: pages
        Loader {
            active: !content.shuttingDown && (index === 0 || content.project !== null)
            sourceComponent: active ? modelData : null
        }
    }
}
