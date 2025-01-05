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
    property int curImageId: -1
    property var curImageSize: null

    DltText {
        anchors.top: parent.top
        anchors.topMargin: 5
        anchors.left: parent.left
        anchors.leftMargin: 5
        id: title
        text: "图像属性:"
        font: DltFont.Subtitle
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
                text: curImageSize ? curImageSize.width + "x" + curImageSize.height : ""
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
        let info = project.getImageInstanceInfo(curImageId)
        imageName.text = info.name
        imagePath.text = info.path
    }
}
