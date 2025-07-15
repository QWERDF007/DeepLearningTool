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

    TestCase {
        name: "DltButtonTest"

        function test_disabledState() {
            buttonItem.disabled = true
            wait(0) // 等待属性应用
            compare(buttonItem.enabled, false)
            compare(buttonItem.opacity, 0.3)
        }

        function test_clickSignal() {
            compare(buttonItem.text, "测试按钮")
            mouseClick(buttonItem)
            compare(buttonItem.text, "Clicked")
        }
    }
}
