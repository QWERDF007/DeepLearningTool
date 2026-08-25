#include <QtQuickTest/quicktest.h>

#include "QmlModelFixture.h"
#include "model/EvaluationThumbnailImageProvider.h"

#include <QString>
#include <QByteArray>
#include <QFileInfo>
#include <QDir>
#include <QtQml/qqml.h>
#include <QQmlEngine>

namespace {

class QuickTestSetup final : public QObject
{
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->addImageProvider(QStringLiteral("evaluationthumbnail"),
                                 new dltool::model::EvaluationThumbnailImageProvider());
    }
};

} // namespace

int main(int argc, char **argv)
{
    // Release 构建产物中的 QML 模块目录；Windows 路径列表分隔符为分号。
    const QString build_root = QStringLiteral("F:/Projects/DeepLearningTool/build");
    const QString import_paths = build_root + QLatin1Char(';') + build_root + QStringLiteral("/qml");
    qputenv("QML2_IMPORT_PATH", import_paths.toUtf8());

    qmlRegisterType<dltool::model::testsupport::QmlModelFixture>("dltool.modeltest", 1, 0,
                                                                  "ModelTestFixture");

    QString qml_test_dir = QFINDTESTDATA("qml");
#ifdef QT_TESTCASE_SOURCEDIR
    if (!QDir(qml_test_dir).exists())
        qml_test_dir = QDir(QStringLiteral(QT_TESTCASE_SOURCEDIR)).filePath(QStringLiteral("qml"));
#endif
    QuickTestSetup setup;
    return quick_test_main_with_setup(argc, argv, "tst_dltool_model_qml", qPrintable(qml_test_dir), &setup);
}

#include "main.moc"
