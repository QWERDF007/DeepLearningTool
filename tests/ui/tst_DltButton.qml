import QtQuick
import QtQuick.Controls
import QtTest

import dltool.ui


TestCase {
    id: testCase
    name: "DltButtonTest"

    Item {
        id: container
        width: 200
        height: 200
        DltButton {
            id: testButton
            text: "测试按钮"
        }
    }

    function test_initialState() {
        testButton.disabled = false
        compare(testButton.enabled, true)
        compare(testButton.opacity, 1)
        compare(testButton.text, "测试按钮")
        compare(testButton.Accessible.role, Accessible.Button)
        compare(testButton.Accessible.description, "")
    }

    function test_disabledState() {
        testButton.disabled = true
        wait(0) // 等待属性应用
        compare(testButton.enabled, false)
        compare(testButton.opacity, 0.3)
    }

    function test_clickSignal() {
        var clicked = false
        testButton.clicked.connect(function () {
            console.log("click on test_clickSignal")
            clicked = true
        })
        mouseClick(testButton)
        // testButton.clicked()
        wait(0)
        compare(clicked, true)
    }
}
