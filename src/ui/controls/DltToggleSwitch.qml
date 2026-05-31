import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts

import dltool.ui

Button {
    property color disableColor: Qt.rgba(233 / 255, 233 / 255, 233 / 255, 1)
    property color checkColor: DltColor.Highlight
    property color hoverColor: Qt.rgba(240 / 255, 240 / 255, 240 / 255, 1)
    property color normalColor: Qt.rgba(253 / 255, 253 / 255, 253 / 255, 1)
    property color borderNormalColor: Qt.rgba(141 / 255, 141 / 255, 141 / 255, 1)
    property color borderCheckColor: DltColor.Highlight
    property color borderDisableColor: Qt.rgba(200 / 255, 200 / 255, 200 / 255, 1)
    property color dotNormalColor: Qt.rgba(93 / 255, 93 / 255, 93 / 255, 1)
    property color dotCheckColor: Qt.rgba(1, 1, 1, 1)
    property color dotDisableColor: Qt.rgba(150 / 255, 150 / 255, 150 / 255, 1)
    property real textSpacing: 6
    property bool textRight: true
    property color textColor: {
        if (!enabled) {
            return Qt.rgba(161 / 255, 161 / 255, 161 / 255, 1)
        }
        return DltColor.FontPrimary
    }
    property var clickListener: function () {
        checked = !checked
    }

    id: control
    Accessible.role: Accessible.Button
    Accessible.name: control.text
    // Accessible.description: contentDescription
    Accessible.onPressAction: control.clicked()
    focusPolicy: Qt.TabFocus
    onClicked: clickListener()
    padding: 0
    horizontalPadding: 0
    onCheckableChanged: {
        if (checkable) {
            checkable = false
        }
    }

    background: Item {
        implicitHeight: 20
        implicitWidth: 40
    }

    contentItem: RowLayout {
        spacing: control.textSpacing
        layoutDirection: control.textRight ? Qt.LeftToRight : Qt.RightToLeft

        Rectangle {
            id: control_backgound
            implicitWidth: background.implicitWidth
            implicitHeight: background.implicitHeight
            radius: height / 2

            DltFocusRectangle {
                visible: control.activeFocus
                radius: parent.radius
            }

            color: {
                if (!enabled) {
                    return disableColor
                }
                if (checked) {
                    return checkColor
                }
                if (hovered) {
                    return hoverColor
                }
                return normalColor
            }
            border.width: 1
            border.color: {
                if (!enabled) {
                    return borderDisableColor
                }
                if (checked) {
                    return borderCheckColor
                }
                return borderNormalColor
            }

            DltTextIcon {
                width: parent.height
                x: checked ? control_backgound.width - width : 0
                scale: {
                    if (pressed) {
                        return 5 / 10
                    }
                    return hovered & enabled ? 7 / 10 : 6 / 10
                }
                iconSource: DltFontIcon.FullCircleMask
                iconSize: 20
                color: {
                    if (!enabled) {
                        return dotDisableColor
                    }
                    if (checked) {
                        return dotCheckColor
                    }
                    return dotNormalColor
                }

                Behavior on scale {
                    NumberAnimation {
                        duration: 167
                        easing.type: Easing.OutCubic
                    }
                }
                Behavior on x {
                    NumberAnimation {
                        duration: 167
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        DltText {
            id: btn_text
            text: control.text
            Layout.alignment: Qt.AlignVCenter
            visible: text !== ""
            color: control.textColor
        }
    }
}
