import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.project

Rectangle { // 侧边栏
    id: sidebar
    color: DltColor.Primary

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 5
        
        DltTextIconButton {
            iconSource: DltFontIcon.ExploreContentSingle
            text: "调整图像大小"
            onClicked: {
                slider.from = Settings.imageCellScaleFrom
                slider.to = Settings.imageCellScaleTo
                slider.value = Settings.imageCellScale
                slider.snapMode = Slider.NoSnap
                slider.stepSize = Settings.imageCellScaleStepSize
                slider.valueUpdateCallback = function(value) {
                    Settings.imageCellScale = value
                }
                openPopup(x,y)
            }
        }
        DltTextIconButton {
            iconSource: DltFontIcon.Brightness
            text: "调整图像亮度"
            onClicked: {
                slider.from = Settings.imageBrightnessFrom
                slider.to = Settings.imageBrightnessTo
                slider.value = Settings.imageBrightness
                slider.snapMode = Slider.SnapAlways
                slider.stepSize = Settings.imageBrightnessStepSize
                slider.valueUpdateCallback = function(value) {
                    Settings.imageBrightness = value
                }
                openPopup(x,y)
            }
        }
        DltTextIconButton {
            iconSource: DltFontIcon.BlueLight
            text: "调整图像对比度"
            onClicked: {
                slider.from = Settings.imageContrastFrom
                slider.to = Settings.imageContrastTo
                slider.value = Settings.imageContrast
                slider.snapMode = Slider.SnapAlways
                slider.stepSize = Settings.imageContrastStepSize
                slider.valueUpdateCallback = function(value) {
                    Settings.imageContrast = value
                }
                openPopup(x,y)
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
        height: 32
        DltSlider {
            id: slider
            anchors.centerIn: parent
            property var valueUpdateCallback: null
            onValueChanged: {
                if (valueUpdateCallback) {
                    valueUpdateCallback(value)
                }
            }
        }
    }

    function openPopup(x, y) {
        let pos = sidebar.mapToItem(Qt.application.activeWindow, x, y)
        popup.x = pos.x - popup.width
        popup.y = pos.y
        popup.open()
    }
}
