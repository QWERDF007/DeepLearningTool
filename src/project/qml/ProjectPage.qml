import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import dltool.project

import "creator"
import "project"

Rectangle {
    width: 1080
    height: 1920
    color: DltColor.Background

    

    DltSplitView {
        anchors.fill: parent
        anchors.margins: 5
        ColumnLayout {
            SplitView.fillHeight: true
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: parent.width / 2
            SplitView.preferredWidth: 300
            ProjectOpener {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
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
                        projectInfo.description = newDescription
                    }
                }
            }
        }

        ProjectHistoryView {
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
}
