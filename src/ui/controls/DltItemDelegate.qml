import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import dltool.ui

T.ItemDelegate {
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    padding: 0
    verticalPadding: 8
    horizontalPadding: 10
    icon.color: control.palette.text
    contentItem:DltText {
        text: control.text
        font: control.font
        color: DltColor.FontPrimary
    }
    background: Rectangle {
        implicitWidth: 100
        implicitHeight: 30
        color: DltColor.Hovered
        visible: control.down || control.highlighted || control.visualFocus
    }
}
