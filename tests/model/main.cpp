#include "../test_runner.h"

#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    return runAllCppTests(argc, argv);
}
