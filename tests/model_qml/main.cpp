#include <QtQuickTest/quicktest.h>

#include <QGuiApplication>
#include <QString>
#include <QByteArray>
#include <QFileInfo>
#include <QDir>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    // Release 构建产物中的 QML 模块目录；Windows 路径列表分隔符为分号。
    const QString build_root = QStringLiteral("F:/Projects/DeepLearningTool/build");
    const QString import_paths = build_root + QLatin1Char(';') + build_root + QStringLiteral("/qml");
    qputenv("QML2_IMPORT_PATH", import_paths.toUtf8());

    return quick_test_main(argc, argv, "tst_dltool_model_qml", qPrintable(QFINDTESTDATA("qml")));
}
