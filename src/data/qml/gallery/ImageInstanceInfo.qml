import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data

Rectangle {
    id: imageInstanceInfo
    color: DltColor.Primary
    clip: true
    property int rowH: 64

    property DataManager dataManager
    property ImageInfoModel imageInfo : dataManager ? dataManager.imageInfo : null
    property int currentImageId: dataManager ? dataManager.imageInstances.currentImageId : -1



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
        anchors.fill: parent
        anchors.margins: 5
        DltText {
            Layout.fillWidth: true
            height: 32
            text: "图像属性:"
            font: DltFont.Subtitle
        }

        ListView {
            id: view
            clip: true
            Layout.fillHeight: true
            Layout.fillWidth: true
            boundsBehavior: Flickable.StopAtBounds
            model: imageInfo
            ScrollBar.vertical: DltScrollBar{}
            delegate: ColumnLayout {
                width: view.width
                spacing: 2
                DltText {
                    Layout.leftMargin: 5
                    Layout.rightMargin: 5
                    text: model.title
                    textColor: DltColor.FontDark
                }
                DltText {
                    id: value
                    Layout.fillWidth: true
                    Layout.leftMargin: 5
                    Layout.rightMargin: 5
                    Layout.bottomMargin: 5
                    text: model.value
                    wrapMode: Text.Wrap
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: function(mouse) {
                            copyboard.text = value.text
                            menu.popup()
                        }
                    }
                }
            }
        }
    }
}
