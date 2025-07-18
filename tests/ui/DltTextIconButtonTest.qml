import QtQuick
import QtQuick.Controls
import QtTest

import dltool.ui

DltTextIconButton {
    id: buttonItem
    text: "测试按钮"
    onClicked: {
        text = "Clicked"
    }

    SignalSpy {
        id: clickSpy
        target: buttonItem
        signalName: "clicked"
    }

    TestCase {
        name: "DltTextIconButtonTest"
        when: windowShown

        function test_click() {
            mouseClick(buttonItem)
            compare(buttonItem.text, "Clicked")
            compare(clickSpy.count, 1)
        }
    }
}
