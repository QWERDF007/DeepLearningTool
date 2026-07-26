import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

// Common centered form dialog used by data-management editors.
QuiPopup {
    id: control

    width: 480
    implicitHeight: contentLayout.implicitHeight + 32
    maskOpacity: 0.2

    property string title: ""
    property string errorText: ""
    property color errorColor: "#D83B01"
    property bool submitEnabled: true
    property string submitText: "确认"
    property string cancelText: "取消"

    default property alias formContent: formLayout.data

    signal accepted()

    function displayMessage(message) {
        if (message.startsWith("error:")) {
            return message.substring(6)
        }
        if (message.startsWith("warning:")) {
            return message.substring(8)
        }
        return message
    }

    ColumnLayout {
        id: contentLayout
        x: 16
        y: 16
        width: control.width - 32
        spacing: 12

        QuiText {
            Layout.fillWidth: true
            text: control.title
            font: QuiFont.Title
            visible: text.length > 0
        }

        ColumnLayout {
            id: formLayout
            Layout.fillWidth: true
            spacing: 8
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumHeight: 32
            Layout.preferredHeight: 32

            QuiText {
                anchors.fill: parent
                text: control.displayMessage(control.errorText)
                color: control.errorColor
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignTop
                visible: text.length > 0
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Item {
                Layout.fillWidth: true
            }

            QuiButton {
                text: control.cancelText
                onClicked: control.close()
            }

            QuiButton {
                text: control.submitText
                enabled: control.submitEnabled
                onClicked: control.accepted()
            }
        }
    }
}
