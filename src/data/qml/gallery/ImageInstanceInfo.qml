import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.data
import quickui
import "../component"

SidebarPanelBase {
    id: imageInstanceInfo
    title: "图像属性:"

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

    ListView {
        id: view
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: imageInfo
        ScrollBar.vertical: QuiScrollBar {}
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
