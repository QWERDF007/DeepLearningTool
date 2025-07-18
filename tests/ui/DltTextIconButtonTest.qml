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

        function init() {
            buttonItem.enabled = true
            clickSpy.clear()
        }

        function test_disabled() { // 测试禁用
            buttonItem.enabled = false
            mouseClick(buttonItem)
            compare(clickSpy.count, 0)
        }

        function test_clicked() {
            mouseClick(buttonItem)
            compare(buttonItem.text, "Clicked")
            compare(clickSpy.count, 1)
        }
    }
}
