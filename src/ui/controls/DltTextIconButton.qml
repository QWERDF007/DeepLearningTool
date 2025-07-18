import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Basic

import dltool.ui

Button {
    display: Button.IconOnly
    property int iconSize: 20
    property int iconSource
    property int radius:4
    property string contentDescription: ""
    property color hoverColor: Qt.lighter(normalColor, 1.2)
    property color pressedColor: Qt.lighter(normalColor, 1.3)
    property color normalColor: DltColor.Button
    // property color disableColor: QuickColor.ItemNormal
    property Component iconDelegate: com_icon
    property color color: pressed ? pressedColor : hovered ? hoverColor : normalColor
    property color iconColor: enabled ? DltColor.FontPrimary : "#6E6E6E"
    property color textColor: DltColor.FontPrimary
    Accessible.role: Accessible.Button
    Accessible.name: control.text
    Accessible.description: contentDescription
    Accessible.onPressAction: control.clicked()
    id:control
    focusPolicy:Qt.TabFocus
    padding: 0
    verticalPadding: 4
    horizontalPadding: 4
    font: DltFont.Caption
    background: Rectangle{
        implicitWidth: 30
        implicitHeight: 30
        radius: control.radius
        color:control.color
        DltFocusRectangle{
            visible: control.activeFocus
        }
    }
    Component{
        id:com_icon
        DltTextIcon {
            id:text_icon
            font.pixelSize: iconSize
            iconSize: control.iconSize
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            iconColor: control.iconColor
            iconSource: control.iconSource
        }
    }
    Component{
        id:com_row
        RowLayout{
            DltLoader{
                sourceComponent: iconDelegate
                Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
                visible: display !== Button.TextOnly
            }
            DltText{
                text:control.text
                Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
                visible: display !== Button.IconOnly
                color: control.textColor
                font: control.font
            }
        }
    }
    Component{
        id:com_column
        ColumnLayout{
            DltLoader{
                sourceComponent: iconDelegate
                Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
                visible: display !== Button.TextOnly
            }
            DltText{
                text:control.text
                Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
                visible: display !== Button.IconOnly
                color: control.textColor
                font: control.font
            }
        }
    }
    contentItem: DltLoader{
        sourceComponent: {
            if(display === Button.TextUnderIcon){
                return com_column
            }
            return com_row
        }
    }
    DltToolTip{
        id:tool_tip
        visible: {
            if(control.text === ""){
                return false
            }
            if(control.display !== Button.IconOnly){
                return false
            }
            return hovered
        }
        text:control.text
        delay: 200
    }
}
