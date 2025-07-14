#include "test_Utils.h"

#include "ui/Utils.h"

#include <QtTest>

namespace dltool::ui {

void UtilsTest::withOpacityTestCase_data()
{
    QTest::addColumn<QColor>("color");
    QTest::addColumn<qreal>("opacity");
    QTest::addColumn<int>("red");
    QTest::addColumn<int>("green");
    QTest::addColumn<int>("blue");
    QTest::addColumn<int>("alpha");

    QTest::newRow("test_withOpacity_1") << QColor(Qt::red) << 0.5 << 255 << 0 << 0 << 128;
    QTest::newRow("test_withOpacity_2") << QColor(Qt::green) << 0.3 << 0 << 255 << 0 << 77;
    QTest::newRow("test_withOpacity_3") << QColor(Qt::blue) << 0.7 << 0 << 0 << 255 << 179;
}

void UtilsTest::withOpacityTestCase()
{
    QFETCH(QColor, color);
    QFETCH(qreal, opacity);
    QFETCH(int, red);
    QFETCH(int, green);
    QFETCH(int, blue);
    QFETCH(int, alpha);

    QColor result = Utils::getInstance()->withOpacity(color, opacity);
    QCOMPARE(result.red(), red);
    QCOMPARE(result.green(), green);
    QCOMPARE(result.blue(), blue);
    QCOMPARE(result.alpha(), alpha);
}

} // namespace dltool::ui
