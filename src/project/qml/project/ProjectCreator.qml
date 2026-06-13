import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform

import dltool.ui
import dltool.project
import quickui

QuiPopup {
    id: creator
    width: 1000
    height: 600
    property alias methods: methodSelection.methods

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        QuiText {
            text: "创建项目"
            font: QuiFont.Subtitle
        }

        MethodSelection {
            id: methodSelection
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        ProjectForm {
            id: projectForm
            Layout.fillWidth: true
            height: 320
            method: methodSelection.method
        }
        Item {
            Layout.fillWidth: true
            height: 32
            QuiButton {
                anchors.right: createBtn.left
                anchors.rightMargin: 5
                width: parent.width / 4
                height: parent.height
                text: "取消"
                onClicked: {
                    creator.close()
                }
            }

            QuiButton {
                id: createBtn
                enabled: projectForm.isValid
                anchors.right: parent.right
                width: parent.width / 4
                height: parent.height
                text: " 创建项目"
                normalColor: QuiColor.Highlight
                onClicked: {
                    creator.close()
                    ProjectManager.createProject(projectForm.name, methodSelection.method, projectForm.path, projectForm.description, projectForm.image_base_path)
                    projectForm.reset()
                }
            }
        }
    }
}
