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
        
        function test_disabledState() {
            buttonItem.disabled = true
            wait(0) // 等待属性应用
            compare(buttonItem.enabled, false)
            compare(buttonItem.opacity, 0.3)
        }

        function test_click() {
            compare(buttonItem.text, "测试按钮")
            mouseClick(buttonItem)
            compare(buttonItem.text, "Clicked")
            compare(clickSpy.count, 1)
        }
    }
}
