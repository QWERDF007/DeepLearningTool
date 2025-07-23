import QtQuick
import QtTest
import dltool.ui

DltEditor {
    id: editorItem

    SignalSpy {
        id: editTextChangedSpy
        target: editorItem
        signalName: "editTextChanged"
    }

    TestCase {
        name: "DltEditorTest"
        when: windowShown

        function init() {
            editTextChangedSpy.clear()
        }

        function cleanup() {
            editorItem.close()            
        }

        function cleanupTestCase() {
            editorItem.close()
            wait(100)
        }

        function test_editTextChanged() {
            editorItem.open()
            verify(editorItem.visible)
            
            // 获取文本编辑区域
            var textEdit = getTextEdit()
            verify(textEdit !== undefined, "TextEdit not found")

            var confirmButton = getButton("确认")
            verify(confirmButton !== undefined, "Confirm button not found")
            
            // 测试文本更改信号
            textEdit.text = "test content"
            mouseClick(confirmButton)
            compare(editTextChangedSpy.count, 1, "editTextChanged signal should be emitted once")
            compare(editTextChangedSpy.signalArguments[0][0], "test content", "EditText should be updated")
            
            wait(100)
            verify(!editorItem.visible, "Editor should close after confirm")
        }

        function test_buttonExistence() {
            editorItem.open()
            var confirmButton = getButton("确认")
            verify(confirmButton !== null, "Save button not found")
            var cancelButton = getButton("取消")
            verify(cancelButton !== null, "Cancel button not found")
            editorItem.close()
            wait(100)
        }

        function test_confirmButton() {
            editorItem.open()
            var confirmButton = getButton("确认")
            mouseClick(confirmButton)
            wait(100)
            verify(!editorItem.visible, "Editor should close after confirm")
        }

        function test_cancelButton() {
            editorItem.open()
            var cancelButton = getButton("取消")
            mouseClick(cancelButton)
            wait(100)
            verify(!editorItem.visible, "Editor should close after cancel")
        }
    }

    function getChildren(index, typeStr) {
        if (index >= 2)
            return null
        // 查找ColumnLayout
        var columnLayout = null
        for (var i = 0; i < editorItem.contentChildren.length; i++) {
            var child = editorItem.contentChildren[i]
            if (child.toString().includes("ColumnLayout")) {
                columnLayout = child
                break
            }
        }
        if (!columnLayout) 
            return null

        // 获取ColumnLayout的子项
        if (columnLayout.children.length < 2)
            return null;
        var container = columnLayout.children[index]
        var items = []
        for (var j = 0; j < container.children.length; j++) {
            var item = container.children[j]
            if (item.toString().includes(typeStr)) {
                items.push(item)
            }
        }
        return items
    }

    function getButton(text) {
        var items = getChildren(1, "DltButton")
        if (!items)
            return null
        for (var i = 0; i < items.length; i++) {
            var item = items[i]
            if (item.text === text) {
                return item
            }
        }
        return null
    }

    function getTextEdit() {
        var items = getChildren(0, "DltTextField")
        if (!items)
            return null
        return items[0]
    }

}
