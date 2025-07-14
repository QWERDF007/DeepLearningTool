#include "test_runner.h"

#include <QtQuickTest/quicktest.h>

#include <QGuiApplication>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    int result = 0;
    result |= runAllTests(argc, argv);
    // 注册测试目录（多个测试用例）
    result |= quick_test_main_with_setup(argc, argv, "UIControlsTest", TEST_QML_SOURCE_DIR, nullptr);
    return result;
}
