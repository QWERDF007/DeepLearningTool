import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import dltool.settings
import quickui

Rectangle { // 侧边栏
    id: sidebar
    color: QuiColor.Primary

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 5
        
        QuiTextIconButton {
            iconSource: QuiFontIcon.ExploreContentSingle
            text: "调整图像大小"
            onClicked: {
                slider.from = GlobalSettings.data.imageCellScaleFrom
                slider.to = GlobalSettings.data.imageCellScaleTo
                slider.value = GlobalSettings.data.imageCellScale
                slider.snapMode = Slider.NoSnap
                slider.stepSize = GlobalSettings.data.imageCellScaleStepSize
                slider.valueUpdateCallback = function(value) {
                    GlobalSettings.data.imageCellScale = value
                }
                openPopup(x,y)
            }
        }
        QuiTextIconButton {
            iconSource: QuiFontIcon.Brightness
            text: "调整图像亮度"
            onClicked: {
                slider.from = GlobalSettings.ui.imageBrightnessFrom
                slider.to = GlobalSettings.ui.imageBrightnessTo
                slider.value = GlobalSettings.ui.imageBrightness
                slider.snapMode = Slider.SnapAlways
                slider.stepSize = GlobalSettings.ui.imageBrightnessStepSize
                slider.valueUpdateCallback = function(value) {
                    GlobalSettings.ui.imageBrightness = value
                }
                openPopup(x,y)
            }
        }
        QuiTextIconButton {
            iconSource: QuiFontIcon.BlueLight
            text: "调整图像对比度"
            onClicked: {
                slider.from = GlobalSettings.ui.imageContrastFrom
                slider.to = GlobalSettings.ui.imageContrastTo
                slider.value = GlobalSettings.ui.imageContrast
                slider.snapMode = Slider.SnapAlways
                slider.stepSize = GlobalSettings.ui.imageContrastStepSize
                slider.valueUpdateCallback = function(value) {
                    GlobalSettings.ui.imageContrast = value
                }
                openPopup(x,y)
            }
        }
        Item {
            Layout.fillHeight: true
        }
    }
    QuiPopup {
        id: popup
        bgColor: QuiColor.Primary
        closePolicy: Popup.CloseOnPressOutside
        width: 200
        height: 32
        QuiSlider {
            id: slider
            anchors.centerIn: parent
            snapMode: Slider.SnapAlways
            property var valueUpdateCallback: null
            onMoved: {
                if (valueUpdateCallback) {
                    valueUpdateCallback(value)
                }
            }
        }
        T.Overlay.modal: null // 不显示遮罩
    }

    function openPopup(x, y) {
        let pos = sidebar.mapToItem(null, x, y)
        popup.x = pos.x - popup.width - 10
        popup.y = pos.y
        popup.open()
    }
}
