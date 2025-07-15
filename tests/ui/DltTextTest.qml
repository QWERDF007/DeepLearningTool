import QtQuick
import QtQuick.Controls
import QtTest

import dltool.ui

DltText {
    id: textItem
    text: "测试文本"

    TestCase {
        name: "DltTextTest"
        // when: windowShown
        function test_text_properties() {
            compare(textItem.text, "测试文本");
            compare(textItem.font, DltFont.Body);
            compare(textItem.color, DltColor.FontPrimary);

            // 测试 textColor alias
            textItem.textColor = "red";
            compare(textItem.color, Qt.color("red"));
        }
    }
}

