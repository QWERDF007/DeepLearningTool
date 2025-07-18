import QtQuick
import QtQuick.Controls

import dltool.ui

DltControlBackground {
    property Item inputItem
    id:control
    color: "red"
    border.width: 1
    gradient: Gradient {
        GradientStop { position: 0.0; color: d.startColor }
        GradientStop { position: 1 - d.offsetSize/control.height; color: d.startColor }
        GradientStop { position: 1.0; color: d.endColor }
    }
    bottomMargin: inputItem && inputItem.activeFocus ? 2 : 1
    QtObject{
        id:d
        property int offsetSize  : inputItem && inputItem.activeFocus ? 2 : 3
        property color startColor: Qt.rgba(232/255,232/255,232/255,1)
        property color endColor: {
            if(!control.enabled){
                return d.startColor
            }
            return inputItem && inputItem.activeFocus ? QuickColor.Primary : Qt.rgba(132/255,132/255,132/255,1)
        }
    }
}
