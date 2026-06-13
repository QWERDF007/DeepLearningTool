import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui

Rectangle {
    id: imageInstanceInfo
    color: QuiColor.Primary
    clip: true
    property int rowH: 64

    property DataManager dataManager
    property ImageInfoModel imageInfo : dataManager ? dataManager.imageInfo : null
    property int currentImageId: dataManager ? dataManager.imageInstances.currentImageId : -1



    QuiMenu {
        id: menu
        width: 200
        QuiMenuItem {
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
        QuiText {
            Layout.fillWidth: true
            height: 32
            text: "图像属性:"
            font: QuiFont.Subtitle
        }

        ListView {
            id: view
            clip: true
            Layout.fillHeight: true
            Layout.fillWidth: true
            boundsBehavior: Flickable.StopAtBounds
            model: imageInfo
            ScrollBar.vertical: QuiScrollBar{}
            delegate: InfoTextItem {
                id: infoItem
                width: view.width
                title: model.title
                text: model.value
                onClicked: {
                    copyboard.text = text
                    menu.popup()
                }
            }
        }
    }
}
