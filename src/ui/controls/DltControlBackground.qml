import QtQuick
import QtQuick.Controls
import dltool.ui

Item{
    id:control
    property int radius: 4
    property bool shadow: true
    property alias border: d.border
    property var bottomMargin: undefined
    property var topMargin: undefined
    property var leftMargin: undefined
    property var rightMargin: undefined
    property color color: DltColor.Button
    property alias gradient : rect_border.gradient
    Rectangle{
        id:d
        property color startColor: Qt.lighter(d.border.color,1.25)
        property color endColor: shadow ? control.border.color : startColor
        visible: false
        border.color: DltColor.ButtonShadow
    }
    Rectangle{
        id:rect_border
        anchors.fill: parent
        radius: control.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: d.startColor }
            GradientStop { position: 1 - 3/control.height; color: d.startColor }
            GradientStop { position: 1.0; color: d.endColor}
        }
    }
    Rectangle{
        id:rect_back
        anchors{
            fill: parent
            margins: control.border.width
            topMargin: control.topMargin
            bottomMargin: control.bottomMargin
            leftMargin: control.leftMargin
            rightMargin: control.rightMargin
        }
        Behavior on anchors.bottomMargin {
            NumberAnimation{
                easing.type: Easing.OutCubic
                duration: 167
            }
        }
        radius: control.radius
        color: control.color
    }
}
