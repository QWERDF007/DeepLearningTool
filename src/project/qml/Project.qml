import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.data
import dltool.project

import "creator"
import "project"



Rectangle {
    width: 1080
    height: 1920
    color: DltColor.Background

    ProjectCreator {
        id: projectCreator
    }

    DltSplitView {
        anchors.fill: parent
        anchors.margins: 5
        ColumnLayout {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 300
            Rectangle {
                color: DltColor.Primary
                Layout.fillWidth: true
                Layout.preferredHeight: 110
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
            }
            ProjectNameType {
                id: projectNameType
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                onProjectNameChanged: function(newName) {
                    let ok = ProjectManager.updateProjectBaseInfo(projectInfo.path, newName, projectInfo.description)
                    if (ok) {
                        projectNameType.name = newName
                    }
                }
            }

            ProjectInfo {
                id: projectInfo
                Layout.fillHeight: true
                Layout.fillWidth: true
                onProjectDescriptionChanged: function(newDescription) {
                    let ok = ProjectManager.updateProjectBaseInfo(projectInfo.path, projectNameType.name, newDescription)
                    if (ok) {
                        console.log("new desc", newDescription)
                        projectInfo.description = newDescription
                    }
                }
            }
        }

        ProjectsView {
            SplitView.fillHeight: true
            SplitView.fillWidth: true
            onPathChanged: {
                let info = ProjectManager.getProjectInfo(path)
                projectNameType.name = info.name
                projectNameType.method = DeepLearningMethod.getMethodName(info.method)
                projectInfo.path = info.path
                projectInfo.description = info.description
                projectInfo.image_base_path = info.image_base_path
                projectInfo.ctime = info.ctime
                projectInfo.mtime = info.mtime
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
