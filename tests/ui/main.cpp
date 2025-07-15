#include "test_runner.h"

#include <QtQuickTest/quicktest.h>

#include <QGuiApplication>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    int result = 0;
    result |= runAllCppTests(argc, argv);
    result |= quick_test_main_with_setup(argc, argv, "dltool::ui::ControlsTest", TEST_QML_SOURCE_DIR, nullptr);
    return result;
}
