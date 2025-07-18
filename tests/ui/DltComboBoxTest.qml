import QtQuick
import QtQuick.Controls
import QtTest

import dltool.ui

DltComboBox {
    id: combo
    editable: true
    model: ["Option A", "Option B", "Option C"]

    SignalSpy {
        id: commitSpy
        target: combo
        signalName: "commit"
    }

    TestCase {
        name: "DltComboBoxTest"
        when: windowShown

        function init() {
            combo.enabled = true
            combo.model = ["Option A", "Option B", "Option C"]
        }

        function cleanup() {

        }

        function test_defaultProperties() {
            verify(combo.enabled)
            verify(combo.editable)
        }

        function test_textCommit() { // 测试编辑文本
            combo.editable = true
            verify(combo.enabled)
            commitSpy.clear()
            // 模拟用户输入
            mouseClick(combo)
            combo.editText = "012"
            keyClick(Qt.Key_Enter)
            // wait(100) // 等信号触发
            compare(commitSpy.count, 1)
            compare(commitSpy.signalArguments[0][0], "012")
        }

        function test_disabled() { // 测试禁用
            combo.enabled = false
            mouseClick(combo.indicator)
            verify(!combo.popup.visible)
        }

        function test_popup() { // 测试点击弹窗
            verify(combo.enabled)
            mouseClick(combo.indicator)
            verify(combo.popup.visible)
            mouseClick(combo.indicator)
            wait(100) // 等弹窗关闭
            verify(!combo.popup.visible)
        }

        function test_selectItem() { // 测试选中选项
            verify(combo.enabled)
            if (combo.count < 2)
                skip("count less than 2, skip test")
            // combo.editable = false // editable 时点击中心会触发编辑
            mouseClick(combo.indicator)
            verify(combo.popup.visible)
            // combo.popup.open()
            let view = combo.popup.contentItem  // ListView
            verify(view !== null)

            let indexToClick = 1 // Option B 的索引
            view.currentIndex = indexToClick
            view.forceActiveFocus()

            let item = view.itemAtIndex(indexToClick)
            verify(item !== null)
            // 模拟点击该项（点击中间位置）
            mouseClick(item)

            // wait(50) // 等模型更新

            compare(combo.currentIndex, indexToClick)
            compare(combo.displayText, "Option B")
        }
    }
}
