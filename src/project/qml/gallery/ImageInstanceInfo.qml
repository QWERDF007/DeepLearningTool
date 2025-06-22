import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle {
    id: imageInstanceInfo
    color: DltColor.Primary
    clip: true
    property int rowH: 64

    property Project project: ProjectManager.currentProject
    property int curImageId: project ? project.imageInstances.curImageId : -1

    DltText {
        anchors.top: parent.top
        anchors.topMargin: 5
        anchors.left: parent.left
        anchors.leftMargin: 5
        id: title
        text: "图像属性:"
        font: DltFont.Subtitle
    }

    DltMenu {
        id: menu
        width: 200
        DltMenuItem {
            text: "复制"
            onTriggered: {
                copyboard.selectAll()
                copyboard.copy()
            }
        }
    }
    TextEdit {
        id: copyboard
        visible: false
    }

    ColumnLayout {
        anchors{
            top: title.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        anchors.margins: 10
        ColumnLayout {
            width: parent.width
            height: rowH
            DltText {
                text: "图像名称:"
                textColor: DltColor.FontDark
            }
            DltText {
                id: imageName
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        copyboard.text = imageName.text
                        menu.popup()
                    }
                }
            }
        }
        ColumnLayout {
            width: parent.width
            height: rowH
            DltText {
                text: "图像路径:"
                textColor: DltColor.FontDark
            }
            DltText {
                id: imagePath
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                // elide: Text.ElideMiddle
                // DltToolTip {
                //     text: imagePath.text
                //     visible: mouseArea.containsMouse && imagePath.truncated
                // }
                // MouseArea {
                //     id: mouseArea 
                //     anchors.fill: parent
                //     hoverEnabled: true
                // }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        copyboard.text = imagePath.text
                        menu.popup()
                    }
                }
            }
        }
        ColumnLayout {
            width: parent.width
            height: rowH
            DltText {
                text: "图像大小:"
                textColor: DltColor.FontDark
            }
            DltText {
                id: imageSize
            }
        }
        ColumnLayout {
            width: parent.width
            height: rowH
            DltText {
                text: "所属数据集:"
                textColor: DltColor.FontDark
            }
            DltText {
                id: datasetName
            }
        }
        ColumnLayout {
            width: parent.width
            height: rowH
            DltText {
                text: "标签实例:"
                textColor: DltColor.FontDark
            }
            DltText {
                id: labelInstance
            }
        }
        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true
        }
    }

    onCurImageIdChanged: {
        if (project) {
            let info = project.getImageInstanceInfo(curImageId)
            imageName.text = info.name
            imagePath.text = info.path
            datasetName.text = info.datasetName
            imageSize.text = info.imageSize
        }
    }
}
