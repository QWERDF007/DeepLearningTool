import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

DltPopup {
    id: control
    closePolicy: Popup.CloseOnPressOutside
    width: 320
    height: 120
    maskOpacity: 0.2
    property alias description: desc.text
    property alias text: edit.text

    signal editTextChanged(string text)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        ColumnLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            DltText {
                id: desc
            }
            DltTextField {
                id: edit
                Layout.fillWidth: true
            }
        }
        RowLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 10
            Item {
                Layout.fillWidth: true
            }
            DltButton {
                text: "取消"
                onClicked: {
                    control.close()
                }
            }
            DltButton {
                text: "确认"
                onClicked: {
                    control.close()
                    editTextChanged(edit.text)
                }
            }
        }
    }
}
