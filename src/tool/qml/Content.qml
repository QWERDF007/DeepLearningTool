import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

StackLayout {
    width: 1080
    height: 1920

    Component {
        id: project_com
        Project {
            anchors.fill: parent
        }
    }

    Component {
        id: dataset_com
        Rectangle {
            anchors.fill: parent
            color: "red"
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
