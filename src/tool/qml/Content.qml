import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project
import dltool.data

StackLayout {
    width: 1080
    height: 1920

    Component {
        id: project_com
        ProjectPage {
            anchors.fill: parent
        }
    }

    Component {
        id: dataset_com
        GalleryPage {
            anchors.fill: parent
        }
    }

    property var pages: [project_com, dataset_com]

    Repeater {
        model: pages
        Loader {
            sourceComponent: {
                return modelData
            }
        }
    }
}
