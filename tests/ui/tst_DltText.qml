import QtQuick
import QtQuick.Controls
import QtTest

import dltool.ui

TestCase {
    id: testCase
    name: "DltTextTest"

    DltText {
        id: textItem
        text: "测试文本"
    }

    function test_properties() {
        compare(textItem.text, "测试文本");
        compare(textItem.font, DltFont.Body);
        compare(textItem.color, DltColor.FontPrimary);

        // 测试 textColor alias
        textItem.textColor = "red";
        compare(textItem.color, Qt.color("red"));
    }
}
