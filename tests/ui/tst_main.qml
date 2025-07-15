// 注意:
// 1. 一个测试程序只能有一个 tst_*.qml 类似 c++ 的 main, 否则在执行时会 crash, 但可以包含多个 TestCase;
// 2. 不能在 tst_*.qml 中创建 Window, 或者会 invalid root;
// 3. 需要在 tst_*.qml 显示包含一个 TestCase 才能被 QtCreator 扫描到;
// 4. 可以在 tst_*.qml 创建包含 TestCase 的其他 qml 来完成将每个测试按文件独立开来;
// 5. 在其他 qml 文件中的 TestCase 在 PASS 无法跳转, FAIL 则可以

import QtQuick
import QtQuick.Controls
import QtTest

import dltool.ui

Item {
    TestCase {
        name: "TestDummy"

        function test_dummy() {

        }
    }

    DltButtonTest {}
    DltTextTest {}
}
