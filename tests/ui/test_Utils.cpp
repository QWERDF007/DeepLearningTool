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

void UtilsTest::scalarValueHelpersTestCase()
{
    const Utils *utils = Utils::getInstance();

    QCOMPARE(utils->stringValue(QVariant(QStringLiteral("YOLOv8"))), QStringLiteral("YOLOv8"));
    QCOMPARE(utils->stringValue(QVariant()), QString());
    QCOMPARE(utils->numberValue(QVariant(QStringLiteral("0.25")), 1.0), 0.25);
    QCOMPARE(utils->numberValue(QVariant(QStringLiteral("bad")), 1.0), 1.0);
    QCOMPARE(utils->boolValue(QVariant(QStringLiteral("true")), false), true);
    QCOMPARE(utils->boolValue(QVariant(QStringLiteral("false")), true), false);
    QCOMPARE(utils->boolValue(QVariant(QStringLiteral("bad")), false), false);
    QCOMPARE(utils->boolValue(QVariant(1), false), true);
    QCOMPARE(utils->isIntegerValueType(QStringLiteral("int")), true);
    QCOMPARE(utils->isIntegerValueType(QStringLiteral("double")), false);
}

void UtilsTest::valueRangeAtTestCase()
{
    const QVariantList range{320, 1536, 32};
    const Utils       *utils = Utils::getInstance();

    QCOMPARE(utils->valueRangeAt(range, 0, 0).toInt(), 320);
    QCOMPARE(utils->valueRangeAt(range, 1, 0).toInt(), 1536);
    QCOMPARE(utils->valueRangeAt(range, 2, 0).toInt(), 32);
    QCOMPARE(utils->valueRangeAt(range, 3, 640).toInt(), 640);
}

void UtilsTest::paramDecimalsTestCase()
{
    const Utils *utils = Utils::getInstance();

    QCOMPARE(utils->paramDecimals(QStringLiteral("int"), QVariantList{1, 100, 1}), 0);
    QCOMPARE(utils->paramDecimals(QStringLiteral("double"), QVariantList{0.0, 1.0, 0.01}), 2);
    QCOMPARE(utils->paramDecimals(QStringLiteral("double"), QVariantList{0.000001, 1.0, 0.0001}, 0.01), 4);
    QCOMPARE(utils->paramDecimals(QStringLiteral("double"), QVariantList{QStringLiteral("1e-06"), 1.0, 0.0001}), 4);
}

void UtilsTest::getCleanPathTestCase()
{
    const Utils *utils = Utils::getInstance();

    QCOMPARE(utils->getCleanPath(QString()), QString());
    QCOMPARE(utils->getCleanPath(QStringLiteral("   ")), QString());
    QCOMPARE(utils->getCleanPath(QUrl()), QString());

#ifdef Q_OS_WIN
    // Windows 本地路径 URL
    QCOMPARE(utils->getCleanPath(QStringLiteral("file:///C:/projects/data")), QStringLiteral("C:/projects/data"));
    QCOMPARE(utils->getCleanPath(QUrl(QStringLiteral("file:///C:/projects/data"))), QStringLiteral("C:/projects/data"));

    // Windows UNC 网络共享 URL (file://host/share/...)
    QCOMPARE(utils->getCleanPath(QStringLiteral("file://192.168.2.87/share/lzw/shimo-all/000-21")),
             QStringLiteral("//192.168.2.87/share/lzw/shimo-all/000-21"));
    QCOMPARE(utils->getCleanPath(QUrl(QStringLiteral("file://192.168.2.87/share/lzw/shimo-all/000-21"))),
             QStringLiteral("//192.168.2.87/share/lzw/shimo-all/000-21"));
    QCOMPARE(utils->getCleanPath(QStringLiteral("file://nas-server/share/dataset")),
             QStringLiteral("//nas-server/share/dataset"));

    // 已经是本地路径 (未带 scheme)
    QCOMPARE(utils->getCleanPath(QStringLiteral("C:/projects/data")), QStringLiteral("C:/projects/data"));
    QCOMPARE(utils->getCleanPath(QStringLiteral("C:\\projects\\data")), QStringLiteral("C:/projects/data"));
    QCOMPARE(utils->getCleanPath(QStringLiteral("\\\\192.168.2.87\\share\\lzw")), QStringLiteral("//192.168.2.87/share/lzw"));
    QCOMPARE(utils->getCleanPath(QStringLiteral("//192.168.2.87/share/lzw")), QStringLiteral("//192.168.2.87/share/lzw"));
#else
    QCOMPARE(utils->getCleanPath(QStringLiteral("file:///home/user/data")), QStringLiteral("/home/user/data"));
    QCOMPARE(utils->getCleanPath(QUrl(QStringLiteral("file:///home/user/data"))), QStringLiteral("/home/user/data"));
    QCOMPARE(utils->getCleanPath(QStringLiteral("/home/user/data")), QStringLiteral("/home/user/data"));
#endif
}

void UtilsTest::toFileUrlTestCase()
{
    const Utils *utils = Utils::getInstance();

    QCOMPARE(utils->toFileUrl(QString()), QString());
    QCOMPARE(utils->toFileUrl(QUrl()), QString());

#ifdef Q_OS_WIN
    // 本地路径转 URL
    QCOMPARE(utils->toFileUrl(QStringLiteral("C:/projects/data/img.jpg")), QStringLiteral("file:///C:/projects/data/img.jpg"));
    QCOMPARE(utils->toFileUrl(QStringLiteral("C:\\projects\\data\\img.jpg")), QStringLiteral("file:///C:/projects/data/img.jpg"));

    // UNC 路径转 URL (必须保持 file://host/share/...)
    QCOMPARE(utils->toFileUrl(QStringLiteral("//192.168.2.87/share/img.jpg")), QStringLiteral("file://192.168.2.87/share/img.jpg"));
    QCOMPARE(utils->toFileUrl(QStringLiteral("\\\\192.168.2.87\\share\\img.jpg")), QStringLiteral("file://192.168.2.87/share/img.jpg"));

    // 已是 file URL 的输入幂等
    QCOMPARE(utils->toFileUrl(QStringLiteral("file:///C:/projects/data/img.jpg")), QStringLiteral("file:///C:/projects/data/img.jpg"));
    QCOMPARE(utils->toFileUrl(QStringLiteral("file://192.168.2.87/share/img.jpg")), QStringLiteral("file://192.168.2.87/share/img.jpg"));
#else
    QCOMPARE(utils->toFileUrl(QStringLiteral("/home/user/data/img.jpg")), QStringLiteral("file:///home/user/data/img.jpg"));
    QCOMPARE(utils->toFileUrl(QStringLiteral("file:///home/user/data/img.jpg")), QStringLiteral("file:///home/user/data/img.jpg"));
#endif
}

} // namespace dltool::ui
