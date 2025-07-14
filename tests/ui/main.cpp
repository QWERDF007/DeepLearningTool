#include "test_runner.h"

#include <QtQuickTest/quicktest.h>

#include <QGuiApplication>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    int result = 0;
    result |= runAllCppTests(argc, argv);
    return result;
}
