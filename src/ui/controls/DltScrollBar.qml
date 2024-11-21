import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Templates as T

import dltool.ui

T.ScrollBar {
    id: control

    property color color : DltColor.ScrollBar
    property color bgColor: DltColor.ScrollBarBackground
    property color pressedColor: Qt.lighter(color,1.2)

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    visible: control.policy !== T.ScrollBar.AlwaysOff
    minimumSize: Math.max(orientation === Qt.Horizontal ? height / width : width / height,0.3)
    QtObject{
        id:d
        property int  minLine : 2
        property int  maxLine : 6
    }
    z: horizontal? 10 : 20
    verticalPadding : vertical ? 15 : 3
    horizontalPadding : horizontal ? 15 : 3
    background: Rectangle{
        id:back_rect
        radius: 5
        color: bgColor
        opacity:{
            if(vertical){
                return d.maxLine === Number(rect_bar.width)
            }
            return d.maxLine === Number(rect_bar.height)
        }
        Behavior on opacity {
            NumberAnimation{
                duration: 50
            }
        }
    }
    DltTextIconButton{
        width: 12
        height: 12
        iconSize: 8
        verticalPadding: 0
        horizontalPadding: 0
        visible: control.horizontal
        opacity: back_rect.opacity
        anchors{
            left: parent.left
            leftMargin: 2
            verticalCenter: parent.verticalCenter
        }
        iconColor: control.color
        iconSource: DltFontIcon.CaretLeftSolid8
        onClicked: {
            control.decrease()
        }
    }
    DltTextIconButton{
        width: 12
        height: 12
        iconSize: 8
        verticalPadding: 0
        horizontalPadding: 0
        iconColor: control.color
        opacity: back_rect.opacity
        anchors{
            right: parent.right
            rightMargin: 2
            verticalCenter: parent.verticalCenter
        }
        visible: control.horizontal
        iconSource: DltFontIcon.CaretRightSolid8
        onClicked: {
            control.increase()
        }
    }
    DltTextIconButton{
        width: 12
        height: 12
        iconSize: 8
        verticalPadding: 0
        horizontalPadding: 0
        iconColor: control.color
        opacity: back_rect.opacity
        anchors{
            top: parent.top
            topMargin: 2
            horizontalCenter: parent.horizontalCenter
        }
        visible: control.vertical
        iconSource: DltFontIcon.CaretUpSolid8
        onClicked: {
            control.decrease()
        }
    }
    DltTextIconButton{
        width: 12
        height: 12
        iconSize: 8
        verticalPadding: 0
        horizontalPadding: 0
        iconColor: control.color
        opacity: back_rect.opacity
        anchors{
            bottom: parent.bottom
            bottomMargin: 2
            horizontalCenter: parent.horizontalCenter
        }
        visible: control.vertical
        iconSource: DltFontIcon.CaretDownSolid8
        onClicked: {
            control.increase()
        }
    }
    contentItem: Item {
        property bool collapsed: (control.policy === T.ScrollBar.AlwaysOn || (control.active && control.size < 1.0))
        implicitWidth: control.interactive ? d.maxLine : d.minLine
        implicitHeight: control.interactive ? d.maxLine : d.minLine
        Rectangle{
            id:rect_bar
            width:  vertical ? d.minLine : parent.width
            height: horizontal  ? d.minLine : parent.height
            color:{
                if(control.pressed){
                    return control.pressedColor
                }
                return control .color
            }
            anchors{
                right: vertical ? parent.right : undefined
                bottom: horizontal ? parent.bottom : undefined
            }
            radius: width / 2
            visible: control.size < 1.0
        }
        states: [
            State{
                name:"show"
                when: contentItem.collapsed
                PropertyChanges {
                    target: rect_bar
                    width:  vertical ? d.maxLine : parent.width
                    height: horizontal  ? d.maxLine : parent.height
                }
            }
            ,State{
                name:"hide"
                when: !contentItem.collapsed
                PropertyChanges {
                    target: rect_bar
                    width:  vertical ? d.minLine : parent.width
                    height: horizontal  ? d.minLine : parent.height
                }
            }
        ]
        transitions:[
            Transition {
                to: "hide"
                SequentialAnimation {
                    PauseAnimation { duration: 450 }
                    NumberAnimation {
                        target: rect_bar
                        properties: vertical ? "width"  : "height"
                        duration: 167
                        easing.type: Easing.OutCubic
                    }
                }
            }
            ,Transition {
                to: "show"
                SequentialAnimation {
                    PauseAnimation { duration: 450 }
                    NumberAnimation {
                        target: rect_bar
                        properties: vertical ? "width"  : "height"
                        duration: 167
                        easing.type: Easing.OutCubic
                    }
                }
            }
        ]
    }
}
