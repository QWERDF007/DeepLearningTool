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

        }

        function cleanup() {
            editorItem.close()            
        }

        function cleanupTestCase() {
            editorItem.close()
            wait(100)
        }

        function test_defaultProperties() {
            verify(!editorItem.visible)
        }

        function test_editTextChanged() {
            editorItem.open()
            wait(100)
            verify(editorItem.visible)
            console.log("contentItem", editorItem.contentChildren[0].children[1].children)
            editorItem.close()
        }
    }

    function getButton() {

    }

}
