import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dltool.ui
import quickui

Item {
    id: control
    clip: true
    height: 24
    width: 200

    property string name: ""
    property string stats: ""
    property int dataset_id: -1
    property real progress: 0
    property bool selected: false
    property bool hovered: false
    property bool filterActive: false
    property int row: -1

    signal clicked(int row, int button, int modifiers)
    signal hoverChanged(int row, bool hovered)
    signal filterClicked(int datasetId)

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: control.selected ? QuiColor.Highlight
                                : control.hovered ? Qt.lighter(QuiColor.Primary, 1.18)
                                                  : Qt.rgba(0, 0, 0, 0)
        border.color: control.selected ? Qt.lighter(QuiColor.Highlight, 1.15)
                                       : control.hovered ? QuiColor.Border
                                                         : Qt.rgba(0, 0, 0, 0)
        border.width: control.selected || control.hovered ? 1 : 0

        Behavior on color {
            ColorAnimation { duration: 80 }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        z: 0

        onClicked: function(mouse) {
            control.clicked(control.row, mouse.button, mouse.modifiers)
        }
        onEntered: control.hoverChanged(control.row, true)
        onExited: control.hoverChanged(control.row, false)
    }

    RowLayout {
        id: layout
        z: 1
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        spacing: 10
        QuiText {
            id: datasetName
            text: control.name
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            elide: Text.ElideRight

            QuiToolTip {
                text: control.name
                delay: 200
                visible: control.hovered && datasetName.truncated
            }
        }
        QuiProgressBar {
            textVisible: visualPosition > 0
            value: control.progress
            Layout.preferredWidth: 80
        }
        QuiText {
            text: control.stats
            Layout.preferredWidth: 60
        }
        QuiTextIconButton {
            text: control.filterActive ? "不过滤" : "过滤"
            iconSource: control.filterActive ? QuiFontIcon.Hide : QuiFontIcon.View
            display: Button.IconOnly
            enabled: control.dataset_id >= 0
            // normalColor: control.filterActive ? QuiColor.Highlight : QuiColor.Button
            implicitWidth: 24
            implicitHeight: 24
            iconSize: 20
            verticalPadding: 0
            horizontalPadding: 0
            onClicked: control.filterClicked(control.dataset_id)
        }
    }
}
