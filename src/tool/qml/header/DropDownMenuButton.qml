import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Templates as T

//import dl.studio.theme 1.0

import dltool.ui


DltButton {
    id: control
    clip: true

    opacity: enabled ? 1.0 : 0.3

    property alias model : popupModel.model


    //    padding: 0
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    spacing: 6

    checkable: true

    contentItem: RowLayout {
        id: container
        //        clip: true
        anchors.centerIn: parent
        DltTextIconButton {
            id: dropDownBtn
            iconSource: DltFontIcon.ChevronDown
            Layout.leftMargin: 5
            Layout.alignment: Qt.AlignLeft
            pressedColor: Qt.lighter(control.pressedColor, 1.2)
            hoverColor:  Qt.lighter(control.hoverColor, 1.2)

            onClicked: {
                if (!popup.visible) {
                    openPopup(x, y)
                } else {
                    popup.close()
                }
            }


            DltPopup {
                id: popup
                padding: 6
                bg.border{
                    color: "black"
                    width: 1
                }
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                ListView {
                    id: popupModel
                    spacing: 5
                    implicitWidth: contentItem.childrenRect.width
                    implicitHeight: contentItem.childrenRect.height
                    delegate: DltCheckBox {
                        text: model.text
                    }
                }
                maskVisible: false
                // T.Overlay.modal: null // 不显示遮罩
            }
        }

        DltText {
            text: control.text
            font: control.font

            color: control.palette.brightText
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            fontSizeMode: Text.Fit
            Layout.rightMargin: 5
        }
    }

    function openPopup(x, y) {
        let pos = control.mapToItem(null, x, y)
        popup.x = pos.x
        popup.y = pos.y + 30
        popup.open()
    }
}
