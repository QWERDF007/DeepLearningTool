import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Templates as T

import dltool.ui

T.Menu {
    property bool animationEnabled: true
    id: control
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    margins: 0
    overlap: 1
    spacing: 0
    delegate: DltMenuItem {}
    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from:0
            to:1
            duration: control.animationEnabled ? 83 : 0
        }
    }
    exit:Transition {
        NumberAnimation {
            property: "opacity"
            from:1
            to:0
            duration: control.animationEnabled ? 83 : 0
        }
    }

    contentItem: ListView {
        id: view
        implicitHeight: contentHeight
        model: control.contentModel
        // spacing: 0
        interactive: Window.window
                     ? contentHeight + control.topPadding + control.bottomPadding > Window.window.height
                     : false
        clip: true
        currentIndex: control.currentIndex
        ScrollBar.vertical: DltScrollBar{}
    }
    background: Rectangle {
        implicitWidth: 150
        implicitHeight: 40
        color: DltColor.Background
        // border.color: "black"
        // border.width: 1
        radius: 5
        DltShadow {
            color: "black"
        }
    }
    T.Overlay.modal: Rectangle {
        color: Utils.withOpacity(control.palette.shadow, 0.5)
    }
    T.Overlay.modeless: Rectangle {
        color: Utils.withOpacity(control.palette.shadow, 0.12)
    }
}
