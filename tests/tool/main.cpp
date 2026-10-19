#include "test_runner.h"

#include <QGuiApplication>

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication app(argc, argv);

    return runAllCppTests(argc, argv);
}
