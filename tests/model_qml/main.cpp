#include <QtQuickTest/quicktest.h>

#include "QmlModelFixture.h"
#include "model/EvaluationThumbnailImageProvider.h"

#include <QString>
#include <QByteArray>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
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
    const QString existing = QString::fromLocal8Bit(qgetenv("QML2_IMPORT_PATH")).trimmed();
    if (existing.isEmpty())
    {
#ifdef DLT_BUILD_DIR
        const QString build_root = QStringLiteral(DLT_BUILD_DIR);
#else
        const QString build_root = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral(".."));
#endif
        const QString import_paths = build_root + QDir::listSeparator() + build_root + QStringLiteral("/qml");
        qputenv("QML2_IMPORT_PATH", import_paths.toLocal8Bit());
    }

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
