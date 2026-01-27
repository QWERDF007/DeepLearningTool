import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic

import dltool.ui

ProgressBar{
    property int duration: 888
    property real strokeWidth: 14
    property bool textVisible: true
    property bool progressVisible: false
    property color color: DltColor.Highlight
    property color backgroundColor: DltColor.Background
    id:control
    indeterminate: false
    onIndeterminateChanged:{
        if(!indeterminate){
            animator_x.duration = 0
            rect_progress.x = 0
            animator_x.duration = control.duration
        }
    }
    background: Rectangle {
        implicitWidth: 150
        implicitHeight: control.strokeWidth
        color: control.backgroundColor
    }
    contentItem: Rectangle {
        clip: true
        color: "#00000000"
        Rectangle {
            id:rect_progress
            width: {
                if(control.indeterminate){
                    return 0.5 * parent.width
                }
                return control.visualPosition * parent.width
            }
            height: parent.height
            color: control.color
            PropertyAnimation on x {
                id:animator_x
                running: control.indeterminate && control.visible
                from: -rect_progress.width
                to:control.width+rect_progress.width
                loops: Animation.Infinite
                duration: control.duration
            }
        }
        DltText{
            visible: control.textVisible
            text: (control.visualPosition * 100).toFixed(2) + "%"
            anchors.centerIn: parent
        }
    }
}
