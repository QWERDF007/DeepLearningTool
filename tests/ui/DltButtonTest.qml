import QtQuick
import QtQuick.Controls
import QtTest

import dltool.ui

DltButton {
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
        name: "DltButtonTest"
        when: windowShown

        function init() {
            buttonItem.enabled = true
            clickSpy.clear()
        }
        
        function test_disabled() { // 测试禁用
            buttonItem.enabled = false
            mouseClick(buttonItem)
            compare(buttonItem.opacity, 0.3)
            compare(clickSpy.count, 0)
        }

        function test_clicked() { // 测试点击
            compare(buttonItem.text, "测试按钮")
            mouseClick(buttonItem)
            compare(buttonItem.text, "Clicked")
            compare(clickSpy.count, 1)
        }
    }
}
