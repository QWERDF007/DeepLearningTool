import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.impl
import QtQuick.Templates as T

import dltool.ui

T.MenuItem {
    property Component iconDelegate : com_icon
    property int iconSpacing: 5
    property int iconSource
    property int iconSize: 16
    property color textColor: DltColor.FontPrimary
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    onImplicitWidthChanged: {
        console.log("menu item implicitwidth", control.text,  implicitWidth)
    }

    padding: 6
    spacing: 6
    icon.width: 24
    icon.height: 24
    icon.color: control.palette.windowText
    height: visible ? implicitHeight : 0
    font: DltFont.Body
    Component{
        id:com_icon
        DltTextIcon{
            id:content_icon
            iconSize: control.iconSize
            iconSource:control.iconSource
            iconColor: DltColor.FontPrimary
        }
    }
    contentItem: Item{
        Row{
            spacing: control.iconSpacing
            readonly property real arrowPadding: control.subMenu && control.arrow ? control.arrow.width + control.spacing : 0
            readonly property real indicatorPadding: control.checkable && control.indicator ? control.indicator.width + control.spacing : 0
            anchors{
                verticalCenter: parent.verticalCenter
                left: parent.left
                leftMargin: (!control.mirrored ? indicatorPadding : arrowPadding)+5
                right: parent.right
                rightMargin: (control.mirrored ? indicatorPadding : arrowPadding)+5
            }
            DltLoader{
                id:loader_icon
                sourceComponent: iconDelegate
                anchors.verticalCenter: parent.verticalCenter
                visible: status === Loader.Ready
            }
            DltText {
                id:content_text
                text: control.text
                font: control.font
                color: control.textColor
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    indicator: DltTextIcon {
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        visible: control.checked
        iconSource: DltFontIcon.CheckMark
    }
    arrow: DltTextIcon {
        x: control.mirrored ? control.leftPadding : control.width - width - control.rightPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        visible: control.subMenu
        iconSource: DltFontIcon.ChevronRightMed
    }
    background: Item {
        implicitWidth: 150
        implicitHeight: 40
        x: 1
        y: 1
        width: control.width - 2
        height: control.height - 2
        Rectangle{
            anchors.fill: parent
            anchors.margins: 3
            radius: 4
            color: control.highlighted ? DltColor.Hovered : DltColor.Background
        }
    }
}
