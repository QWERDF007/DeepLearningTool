import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle { // 侧边栏
    color: DltColor.Primary

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 5
        
        DltTextIconButton {
            iconSource: DltFontIcon.ExploreContentSingle
            text: "调整图像大小"
            onClicked: {
                popup.open()
            }
        }
        DltTextIconButton {
            iconSource: DltFontIcon.Brightness
            text: "调整图像亮度"
            onClicked: {
                popup.open()
            }
        }
        DltTextIconButton {
            iconSource: DltFontIcon.BlueLight
            text: "调整图像对比度"
            onClicked: {
                popup.open()
            }
        }
        Item {
            Layout.fillHeight: true
        }
    }
    DltPopup {
        id: popup
        closePolicy: Popup.CloseOnPressOutside
        width: 200
        height: 48
        DltSlider {
            id: slider
        }
    }
}
