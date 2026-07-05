import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project
import dltool.data
import dltool.model
import dltool.feature

StackLayout {
    id: content
    width: 1080
    height: 1920

    property Project project : ProjectManager.currentProject
    property DataManager dataManager: project ? project.dataManager : null
    property FeatureManager featureManager: project ? project.featureManager : null
    property bool shuttingDown: false
    property int projectGeneration: 0

    onProjectChanged: projectGeneration += 1

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
            featureManager: content.featureManager
        }
    }

    Component {
        id: label_com
        LabelPage {
            anchors.fill: parent
            dataManager: content.dataManager
            featureManager: content.featureManager
        }
    }

    Component {
        id: review_com
        ReviewPage {
            anchors.fill: parent
            dataManager: content.dataManager
            featureManager: content.featureManager
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
            id: pageLoader
            readonly property bool shouldLoad: !content.shuttingDown && (index === 0 || content.project !== null)
            readonly property int reloadGeneration: content.projectGeneration

            Component.onCompleted: syncSource()
            onShouldLoadChanged: syncSource()
            onReloadGenerationChanged: syncSource()

            function syncSource() {
                sourceComponent = null
                if (shouldLoad) {
                    sourceComponent = modelData
                }
            }
        }
    }
}
