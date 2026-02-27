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

    // 监听 project 变化
    onProjectChanged: {
        console.log("[Content] project changed:", project);
    }

    // 监听 dataManager 变化
    onDataManagerChanged: {
        console.log("[Content] dataManager changed:", dataManager);
    }

    // 监听 currentIndex 变化
    onCurrentIndexChanged: {
        console.log("[Content] currentIndex changed to:", currentIndex);
    }

    Component.onCompleted: {
        console.log("[Content] Component completed");
        console.log("[Content] project:", project);
        console.log("[Content] dataManager:", dataManager);
        console.log("[Content] currentIndex:", currentIndex);
    }

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
            sourceComponent: {
                return modelData
            }
            
            onLoaded: {
                console.log("[Content] Loader", index, "loaded, item:", item);
                if (item) {
                    console.log("[Content] Loader", index, "item.dataManager:", item.dataManager);
                }
            }
        }
    }
}
