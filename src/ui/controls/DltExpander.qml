import QtQuick
import QtQuick.Controls

import dltool.ui

Item {
    property string headerText: ""
    property int headerHeight: 45
    property Component headerDelegate: com_header
    property bool expand: false
    property int contentHeight: 300
    default property alias content: container.data

    id: control
    implicitHeight: Math.max(layout_header.height + layout_container.height, layout_header.height)
    implicitWidth: 400

    QtObject {
        id: d
        property bool flag: false
        function toggle() {
            d.flag = true
            expand = !expand
            d.flag = false
        }
    }

    clip: true

    Component {
        id: com_header
        Row {
            anchors.fill: parent
            spacing: 8

            DltTextIconButton {
                anchors.verticalCenter: parent.verticalCenter
                display: Button.IconOnly
                iconSource: DltFontIcon.ChevronRight
                iconSize: 12
                normalColor: DltColor.Button
                hoverColor: DltColor.Hovered

                iconDelegate: Component {
                    DltTextIcon {
                        rotation: expand ? 90 : 0
                        iconSize: 12
                        iconSource: DltFontIcon.ChevronRight
                        iconColor: DltColor.FontPrimary

                        Behavior on rotation {
                            NumberAnimation {
                                duration: 167
                                easing.type: Easing.OutCubic
                            }
                        }
                    }
                }

                onClicked: d.toggle()
            }

            DltText {
                text: control.headerText
                anchors.verticalCenter: parent.verticalCenter
                color: DltColor.FontPrimary
            }
        }
    }

    Rectangle {
        id: layout_header
        width: parent.width
        height: control.headerHeight
        radius: 4
        border.color: DltColor.Border
        color: DltColor.Primary

        MouseArea {
            id: control_mouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: d.toggle()
        }

        DltLoader {
            anchors.fill: parent
            anchors.leftMargin: 15
            sourceComponent: control.headerDelegate
        }
    }

    Item {
        id: layout_container
        anchors {
            top: layout_header.bottom
            topMargin: -1
            left: layout_header.left
        }
        visible: contentHeight + container.anchors.topMargin !== 0
        height: contentHeight + container.anchors.topMargin
        width: parent.width
        z: -999
        clip: true

        Rectangle {
            id: container
            anchors.fill: parent
            radius: 4
            clip: true
            color: DltColor.Primary
            border.color: DltColor.Border
            anchors.topMargin: -contentHeight

            states: [
                State {
                    name: "expand"
                    when: control.expand
                    PropertyChanges {
                        target: container
                        anchors.topMargin: 0
                    }
                },
                State {
                    name: "collapsed"
                    when: !control.expand
                    PropertyChanges {
                        target: container
                        anchors.topMargin: -contentHeight
                    }
                }
            ]

            transitions: [
                Transition {
                    to: "expand"
                    NumberAnimation {
                        properties: "anchors.topMargin"
                        duration: d.flag ? 167 : 0
                        easing.type: Easing.OutCubic
                    }
                },
                Transition {
                    to: "collapsed"
                    NumberAnimation {
                        properties: "anchors.topMargin"
                        duration: d.flag ? 167 : 0
                        easing.type: Easing.OutCubic
                    }
                }
            ]
        }
    }
}
