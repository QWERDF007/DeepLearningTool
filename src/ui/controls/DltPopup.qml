import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls


import dltool.ui

Popup {
    id: control
    property alias bg: bg
    property real maskOpacity: 0.5
    property bool maskVisible: true
    property color bgColor: DltColor.Background
    padding: 0
    modal:true
    parent: Overlay.overlay
    x: Math.round((d.parentWidth - width) / 2)
    y: Math.round((d.parentHeight - height) / 2)
    closePolicy: Popup.NoAutoClose
    enter: Transition {
        NumberAnimation {
            property: "opacity"
            duration: 83
            from:0
            to:1
        }
    }
    height: Math.min(implicitHeight,d.parentHeight)
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            duration: 83
            from:1
            to:0
        }
    }
    background: Rectangle {
        id: bg
        radius: [5,5,5,5]
        color: control.bgColor
        DltShadow {
            radius: 5
        }
    }
    QtObject{
        id:d
        property int parentHeight: {
            if(control.parent){
                return control.parent.height
            }
            return control.height
        }
        property int parentWidth: {
            if(control.parent){
                return control.parent.width
            }
            return control.width
        }
    }

    T.Overlay.modal: Rectangle {
        visible: maskVisible
        color: Utils.withOpacity("#F0F0F0", maskOpacity)
    }
}
