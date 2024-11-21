import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window

import dltool.ui

Page {
    property string url : ""
    id: control
    StackView.onRemoved: destroy()
    padding: 5
    visible: false
    opacity: visible
    transform: Translate {
        y: control.visible ? 0 : 80
        Behavior on y{
            enabled: true
            NumberAnimation{
                duration: 167
                easing.type: Easing.OutCubic
            }
        }
    }
    Behavior on opacity {
        enabled: true
        NumberAnimation{
            duration: 83
        }
    }
    background: Item{}
    header: DltLoader{
        sourceComponent: control.title === "" ? undefined : com_header
    }
    Component{
        id: com_header
        Item{
            implicitHeight: 40
            DltText{
                id:text_title
                text: control.title
                font: FluTextStyle.Title
                anchors{
                    left: parent.left
                    leftMargin: 5
                }
            }
        }
    }
    Component.onCompleted: {
        control.visible = true
    }
}
