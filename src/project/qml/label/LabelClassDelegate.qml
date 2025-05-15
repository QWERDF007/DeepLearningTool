import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui

Rectangle {
    id: control
    width: parent.width
    height: 32
    color: Qt.lighter(DltColor.Primary, 1.2)
    property string className: ""
    property color classColor: "black"
    property int classId

    signal editClicked
    signal deleteClicked
    signal clicked

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onClicked: function(mouse) {
            control.clicked()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 5
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: height
            radius: 3
            color: control.classColor
        }
        DltText {
            text: control.className
            Layout.fillWidth: true
        }
    }
    RowLayout {
        anchors{
            top: parent.top
            bottom: parent.bottom
            right: parent.right
            rightMargin: 5
            // margins: 5
        }
        spacing: 3
        DltTextIconButton {
            iconSource: DltFontIcon.Edit
            onClicked: {
                control.editClicked()
            }
            normalColor: control.color
        }
        DltTextIconButton {
            iconSource: DltFontIcon.Delete
            onClicked: {
                control.deleteClicked()
            }
            normalColor: control.color
        }
    }
}
