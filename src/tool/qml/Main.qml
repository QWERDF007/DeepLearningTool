import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.tool
import dltool.project

ApplicationWindow {
    id: window
    visible: true
    width: Screen.width
    height: Screen.height
    title: ProjectManager.currentProject ? ProjectManager.currentProject.path : ""

    Connections {
        target: ProjectManager
        function onCurrentProjectChanged() {
            if (ProjectManager.currentProject) {
                _header.currentIndex = 1
            }
        }
    }

    header: Header {
        id: _header
        width: parent.width
        height: 48
    }


    Content {
        id: content
        anchors {
            top: parent.top
            bottom: parent.bottom
        }
        height: parent.heght
        width: parent.width
        Layout.fillHeight: true
        Layout.fillWidth: true
        currentIndex: _header.currentIndex
    }


    footer: Rectangle {
        width: parent.width
        height: 32
        color: DltColor.Primary
    }

    Component.onCompleted: {
        window.showMaximized()
    }
}
