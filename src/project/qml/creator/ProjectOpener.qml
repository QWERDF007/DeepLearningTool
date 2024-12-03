import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.project

Rectangle {
    color: DltColor.Primary
    width: 400
    height: 200

    ProjectCreator {
        id: projectCreator
    }

    Column {
        anchors {
            top: parent.top
            topMargin: 10
            left: parent.left
            leftMargin: 10
            right: parent.right
            rightMargin: 10
        }
        spacing: 8

        DltButton {
            width: parent.width
            height: 40
            text: "创建项目"
            onClicked: {
                projectCreator.open()
            }
        }

        DltButton {
            width: parent.width
            height: 40
            text: "打开项目"
            onClicked: {
                fileDialog.open()
            }
        }
    }

    FileDialog {
        id: fileDialog
        folder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        nameFilters: [ProjectManager.projectFileFilter()]
        onAccepted: {
            var path = fileDialog.file.toString().slice(8)
            ProjectManager.openProject(path)
        }
    }
}
